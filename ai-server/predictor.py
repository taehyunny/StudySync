"""
StudySync AI 서버 — TCN 모델 로드 + 추론 모듈
================================================
tcp_server.py 에서 import해서 사용

사용 예시:
    from predictor import load_model, predict

    model, scaler_mean, scaler_std, label_names = load_model()
    state, confidence = predict(model, scaler_mean, scaler_std, label_names, buffer)
"""

import logging
import numpy as np
import torch
import torch.nn as nn
from pathlib import Path

logger = logging.getLogger(__name__)

# 모델 경로
THIS_FILE     = Path(__file__).resolve()
AI_SERVER_DIR = THIS_FILE.parent
STUDYSYNC_DIR = AI_SERVER_DIR.parent
MODEL_PATH    = STUDYSYNC_DIR / "dataset" / "model" / "tcn_v11.pt"


# ──────────────────────────────────────────
# TCN 모델 정의
# ──────────────────────────────────────────

class Chomp1d(nn.Module):
    def __init__(self, chomp_size):
        super().__init__()
        self.chomp_size = chomp_size

    def forward(self, x):
        return x[:, :, :-self.chomp_size].contiguous() if self.chomp_size > 0 else x


class TemporalBlock(nn.Module):
    def __init__(self, n_inputs, n_outputs, kernel_size,
                 stride, dilation, padding, dropout):
        super().__init__()
        self.conv1  = nn.utils.weight_norm(
            nn.Conv1d(n_inputs, n_outputs, kernel_size,
                      stride=stride, padding=padding, dilation=dilation))
        self.chomp1 = Chomp1d(padding)
        self.relu1  = nn.ReLU()
        self.drop1  = nn.Dropout(dropout)

        self.conv2  = nn.utils.weight_norm(
            nn.Conv1d(n_outputs, n_outputs, kernel_size,
                      stride=stride, padding=padding, dilation=dilation))
        self.chomp2 = Chomp1d(padding)
        self.relu2  = nn.ReLU()
        self.drop2  = nn.Dropout(dropout)

        self.net = nn.Sequential(
            self.conv1, self.chomp1, self.relu1, self.drop1,
            self.conv2, self.chomp2, self.relu2, self.drop2,
        )
        self.downsample = (nn.Conv1d(n_inputs, n_outputs, 1)
                           if n_inputs != n_outputs else None)
        self.relu = nn.ReLU()

    def forward(self, x):
        out = self.net(x)
        res = x if self.downsample is None else self.downsample(x)
        return self.relu(out + res)


class StudySyncTCN(nn.Module):
    """
    입력: (batch, 7, 150)
    출력: (batch, 3) → focus / distracted / drowsy
    """
    def __init__(self, input_size, num_classes,
                 num_channels, kernel_size, dropout):
        super().__init__()
        layers = []
        for i, out_ch in enumerate(num_channels):
            in_ch    = input_size if i == 0 else num_channels[i - 1]
            dilation = 2 ** i
            padding  = (kernel_size - 1) * dilation
            layers.append(TemporalBlock(
                in_ch, out_ch, kernel_size,
                stride=1, dilation=dilation,
                padding=padding, dropout=dropout
            ))
        self.tcn = nn.Sequential(*layers)
        self.classifier = nn.Sequential(
            nn.Linear(num_channels[-1], 64),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(64, num_classes),
        )

    def forward(self, x):
        out = self.tcn(x)
        return self.classifier(out[:, :, -1])


# ──────────────────────────────────────────
# Public API
# ──────────────────────────────────────────

def load_model(model_path: Path = MODEL_PATH):
    """
    학습된 TCN 모델 로드
    반환: (model, scaler_mean, scaler_std, label_names)
    """
    if not model_path.exists():
        raise FileNotFoundError(f"❌ 모델 파일 없음: {model_path}")

    ckpt = torch.load(model_path, map_location="cpu", weights_only=False)

    model = StudySyncTCN(
        input_size   = ckpt["input_dim"],
        num_classes  = ckpt["num_classes"],
        num_channels = ckpt["tcn_channels"],
        kernel_size  = ckpt["kernel_size"],
        dropout      = ckpt["dropout"],
    )
    model.load_state_dict(ckpt["model_state_dict"])
    model.eval()

    scaler_mean = np.array(ckpt["scaler_mean"], dtype=np.float32)
    scaler_std  = np.array(ckpt["scaler_std"],  dtype=np.float32)
    label_names = ckpt["label_names"]

    logger.info(f"✅ 모델 로드: {model_path.name}")
    logger.info(f"   버전: v{ckpt.get('version','?')}  "
                f"val={ckpt.get('val_accuracy',0)*100:.1f}%  "
                f"test={ckpt.get('test_accuracy',0)*100:.1f}%")
    logger.info(f"   클래스: {label_names}")

    return model, scaler_mean, scaler_std, label_names


def predict(model, scaler_mean, scaler_std, label_names,
            buffer: list,
            baseline_ear: float = None) -> tuple[str, float]:
    """
    150프레임 버퍼 → TCN 추론
    반환: (state, confidence)
      state:        "focus" | "distracted" | "drowsy"
      confidence:   0.0 ~ 1.0
      baseline_ear: 캘리브레이션으로 측정된 기준 EAR
                    (None이면 비율 변환 없이 원본값 사용)
    """
    seq = np.array(buffer, dtype=np.float32)  # (150, 7)

    # EAR 비율 변환 (학습 방식과 동일하게)
    # EAR은 0번 인덱스
    if baseline_ear is not None and baseline_ear > 0.01:
        seq[:, 0] = seq[:, 0] / baseline_ear
    else:
        # 캘리브레이션 없으면 버퍼 첫 30프레임 EAR 평균으로 자체 기준 계산
        self_baseline = seq[:30, 0].mean()
        if self_baseline > 0.01:
            seq[:, 0] = seq[:, 0] / self_baseline

    # 정규화 후 추론
    seq    = (seq - scaler_mean) / scaler_std
    tensor = torch.tensor(seq).unsqueeze(0).permute(0, 2, 1)

    with torch.no_grad():
        probs = torch.softmax(model(tensor), dim=1).squeeze()
        pred  = probs.argmax().item()

    return label_names[pred], round(probs[pred].item(), 4)