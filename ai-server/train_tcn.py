"""
StudySync TCN 학습 스크립트 (Phase 2)
======================================
3클래스 시계열 분류 모델 학습

폴더 구조:
    dataset/focus/labels.csv       → focus      (label=0)
    dataset/distracted/labels.csv  → distracted (label=1)
    dataset/drowsy/labels.csv      → drowsy     (label=2)

    같은 폴더 안의 labels.csv, labels.csv1 등 모두 자동으로 합쳐서 읽음

데이터 분할:
    focus 800 시퀀스        : train 640 / val 80 / test 80
    distracted 400 시퀀스   : train 320 / val 40 / test 40
    drowsy 400 시퀀스       : train 320 / val 40 / test 40

입력 형식:
    1 시퀀스 = 150프레임 × 7개 특징값
    (ear, neck_angle, shoulder_diff, head_yaw, head_pitch,
     face_detected, phone_detected)

저장 위치:
    dataset/model/tcn_v1.pt, tcn_v2.pt, ...
    dataset/model/tcn_best.pt  (항상 최신 버전 복사본)

실행 방법:
    pip install torch scikit-learn pandas
    python train_tcn.py
"""

import random
import shutil
from pathlib import Path
from datetime import datetime

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from sklearn.metrics import classification_report, confusion_matrix
import torch.nn.functional as F


# ──────────────────────────────────────────
# 설정
# ──────────────────────────────────────────

# 경로 자동 설정
THIS_FILE     = Path(__file__).resolve()
AI_SERVER_DIR = THIS_FILE.parent
STUDYSYNC_DIR = AI_SERVER_DIR.parent
BASE_DIR      = STUDYSYNC_DIR / "dataset"

FOCUS_CSV      = BASE_DIR / "focus"      / "labels.csv"
DROWSY_CSV     = BASE_DIR / "drowsy"     / "labels.csv"
DISTRACTED_CSV = BASE_DIR / "distracted" / "labels.csv"

# 모델 저장 위치
MODEL_DIR  = BASE_DIR / "model"
MODEL_PATH = MODEL_DIR / "tcn_best.pt"

# 라벨 매핑 (3분류)
LABEL_MAP = {
    "focus":      0,
    "distracted": 1,
    "drowsy":     2,
}
LABEL_NAMES = ["focus", "distracted", "drowsy"]

# 시퀀스 설정
SEQUENCE_LENGTH = 150
INPUT_DIM       = 7

# 데이터 분할 (시퀀스 단위)
SPLIT_PER_CLASS = {
    "focus":      {"train": 640, "val": 80, "test": 80},
    "distracted": {"train": 320, "val": 40, "test": 40},
    "drowsy":     {"train": 320, "val": 40, "test": 40},
}

# 사용할 특징 컬럼
FEATURE_COLUMNS = [
    "ear", "neck_angle", "shoulder_diff",
    "head_yaw", "head_pitch",
    "face_detected", "phone_detected",
]

# ──────────────────────────────────────────
# 하이퍼파라미터
# ──────────────────────────────────────────

RANDOM_SEED   = 42
BATCH_SIZE    = 8
LEARNING_RATE = 0.0005   # 안정적 학습률
NUM_EPOCHS    = 200      # Early Stopping이 알아서 끊어줌
DROPOUT       = 0.4      # 과적합 방지
TCN_CHANNELS  = [32, 64, 64, 128]   # 기본 4-layer TCN
KERNEL_SIZE   = 5        # 기본 커널 크기
PATIENCE      = 30       # val 정확도 30에폭 동안 안 오르면 조기 종료

# 클래스 가중치 (distracted/drowsy 오분류에 2배 페널티)
CLASS_WEIGHTS = [1.0, 1.0, 1.0]   # focus, distracted, drowsy (균등)


# ──────────────────────────────────────────
# 1. CSV 로드 및 시퀀스 생성
# ──────────────────────────────────────────

def load_class_sequences(csv_path: Path, class_name: str) -> np.ndarray:
    """
    CSV 파일을 로드해서 150프레임 시퀀스 단위로 분할
    같은 폴더 안의 labels.csv, labels.csv1 등 모두 합쳐서 읽음
    반환: shape (N_sequences, 150, 7)
    """
    folder = csv_path.parent

    # 폴더 안의 모든 labels.csv 관련 파일 수집
    csv_files = sorted([
        f for f in folder.iterdir()
        if f.name.startswith("labels.csv") and f.is_file()
    ])

    if not csv_files:
        raise FileNotFoundError(f"❌ CSV 파일 없음: {folder}")

    # 모든 파일 합치기
    dfs = []
    for f in csv_files:
        try:
            df_part = pd.read_csv(f)
            dfs.append(df_part)
            print(f"  📄 {class_name}: {f.name} → {len(df_part)}개 프레임")
        except Exception as e:
            print(f"  ⚠️  {f.name} 읽기 실패: {e}")

    df = pd.concat(dfs, ignore_index=True)
    print(f"     합계: {len(df)}개 프레임")

    # 7개 특징값만 추출
    features = df[FEATURE_COLUMNS].values.astype(np.float32)

    # 150프레임씩 자르기
    n_total  = len(features)
    n_seq    = n_total // SEQUENCE_LENGTH

    if n_seq == 0:
        raise ValueError(
            f"❌ {class_name}: 시퀀스 0개 "
            f"(최소 {SEQUENCE_LENGTH}프레임 필요, 현재 {n_total})"
        )

    usable   = n_seq * SEQUENCE_LENGTH
    sequences = features[:usable].reshape(n_seq, SEQUENCE_LENGTH, INPUT_DIM)

    discarded = n_total - usable
    if discarded > 0:
        print(f"     ⚠️  남는 프레임 {discarded}개 폐기")

    # EAR 시퀀스별 비율 변환 (첫 30프레임 기준)
    sequences = normalize_ear_by_sequence(sequences, baseline_frames=30)
    print(f"     → {n_seq} 시퀀스 생성 (EAR 비율 변환 완료)")
    return sequences


def split_class_data(sequences: np.ndarray, class_name: str, split_config: dict):
    """시퀀스를 train/val/test로 분할"""
    rng = np.random.RandomState(RANDOM_SEED)
    indices = np.arange(len(sequences))
    rng.shuffle(indices)
    sequences = sequences[indices]

    needed = split_config["train"] + split_config["val"] + split_config["test"]

    if len(sequences) < needed:
        print(f"     ⚠️  {class_name}: 요청 {needed} / 실제 {len(sequences)} → 비율 조정")
        ratio   = len(sequences) / needed
        n_train = int(split_config["train"] * ratio)
        n_val   = int(split_config["val"]   * ratio)
        n_test  = len(sequences) - n_train - n_val
    else:
        n_train = split_config["train"]
        n_val   = split_config["val"]
        n_test  = split_config["test"]
        sequences = sequences[:needed]

    train = sequences[:n_train]
    val   = sequences[n_train : n_train + n_val]
    test  = sequences[n_train + n_val : n_train + n_val + n_test]

    print(f"     분할: train {len(train)} / val {len(val)} / test {len(test)}")
    return train, val, test


def load_all_data() -> dict:
    """모든 클래스 데이터 로드 후 train/val/test 분할"""
    print("\n📂 데이터셋 로드 중...")

    csv_paths = {
        "focus":      FOCUS_CSV,
        "distracted": DISTRACTED_CSV,
        "drowsy":     DROWSY_CSV,
    }

    train_X, train_y = [], []
    val_X,   val_y   = [], []
    test_X,  test_y  = [], []

    for class_name, csv_path in csv_paths.items():
        seqs = load_class_sequences(csv_path, class_name)
        tr, va, te = split_class_data(seqs, class_name, SPLIT_PER_CLASS[class_name])
        label = LABEL_MAP[class_name]

        train_X.append(tr);  train_y.append(np.full(len(tr), label, dtype=np.int64))
        val_X.append(va);    val_y.append(np.full(len(va),   label, dtype=np.int64))
        test_X.append(te);   test_y.append(np.full(len(te),  label, dtype=np.int64))

    train_X = np.concatenate(train_X); train_y = np.concatenate(train_y)
    val_X   = np.concatenate(val_X);   val_y   = np.concatenate(val_y)
    test_X  = np.concatenate(test_X);  test_y  = np.concatenate(test_y)

    print(f"\n✅ 분할 결과:")
    print(f"   Train: {len(train_X)} 시퀀스")
    print(f"   Val  : {len(val_X)} 시퀀스")
    print(f"   Test : {len(test_X)} 시퀀스")

    return {
        "train": (train_X, train_y),
        "val":   (val_X,   val_y),
        "test":  (test_X,  test_y),
    }


# ──────────────────────────────────────────
# 2. EAR 시퀀스별 비율 변환
# ──────────────────────────────────────────

def normalize_ear_by_sequence(sequences: np.ndarray,
                               baseline_frames: int = 30) -> np.ndarray:
    """
    각 시퀀스의 첫 N프레임 EAR 평균을 기준으로 비율 변환
    - EAR 컬럼 인덱스: 0번
    - 눈 뜬 상태: EAR ≈ 1.0 (기준과 동일)
    - 눈 감은 상태: EAR ≈ 0.4~0.5 (기준의 절반 이하)
    - 사람마다 다른 눈 크기/카메라 거리 영향 제거
    """
    result = sequences.copy()
    skipped = 0
    for i in range(len(sequences)):
        baseline_ear = sequences[i, :baseline_frames, 0].mean()
        if baseline_ear > 0.01:
            result[i, :, 0] = sequences[i, :, 0] / baseline_ear
        else:
            skipped += 1
    if skipped > 0:
        print(f"     ⚠️  EAR 기준값 너무 작아 스킵: {skipped}시퀀스")
    return result


# ──────────────────────────────────────────
# 3. 정규화
# ──────────────────────────────────────────

def fit_scaler(X: np.ndarray):
    flat = X.reshape(-1, X.shape[-1])
    mean = flat.mean(axis=0)
    std  = flat.std(axis=0) + 1e-6
    return mean, std


def apply_scaler(X: np.ndarray, mean, std) -> np.ndarray:
    return ((X - mean) / std).astype(np.float32)


# ──────────────────────────────────────────
# 3. Focal Loss
# ──────────────────────────────────────────

class FocalLoss(nn.Module):
    """
    Focal Loss — 어려운 샘플(distracted↔drowsy 경계)에 자동으로 집중
    gamma: 높을수록 어려운 샘플에 더 집중 (보통 2.0)
    """
    def __init__(self, weight=None, gamma=2.0):
        super().__init__()
        self.weight = weight
        self.gamma  = gamma

    def forward(self, inputs, targets):
        ce_loss = F.cross_entropy(
            inputs, targets, weight=self.weight, reduction="none"
        )
        pt = torch.exp(-ce_loss)
        focal_loss = (1 - pt) ** self.gamma * ce_loss
        return focal_loss.mean()


# ──────────────────────────────────────────
# 4. PyTorch Dataset
# ──────────────────────────────────────────

class FocusSequenceDataset(Dataset):
    def __init__(self, X: np.ndarray, y: np.ndarray):
        # (N, T, F) → (N, F, T) for Conv1d
        self.X = torch.tensor(X, dtype=torch.float32).permute(0, 2, 1)
        self.y = torch.tensor(y, dtype=torch.long)

    def __len__(self):
        return len(self.X)

    def __getitem__(self, idx):
        return self.X[idx], self.y[idx]


# ──────────────────────────────────────────
# 4. TCN 모델
# ──────────────────────────────────────────

class Chomp1d(nn.Module):
    def __init__(self, chomp_size):
        super().__init__()
        self.chomp_size = chomp_size

    def forward(self, x):
        return x[:, :, :-self.chomp_size].contiguous() if self.chomp_size > 0 else x


class TemporalBlock(nn.Module):
    def __init__(self, n_inputs, n_outputs, kernel_size, stride, dilation, padding, dropout):
        super().__init__()
        self.conv1   = nn.utils.weight_norm(
            nn.Conv1d(n_inputs, n_outputs, kernel_size,
                      stride=stride, padding=padding, dilation=dilation))
        self.chomp1  = Chomp1d(padding)
        self.relu1   = nn.ReLU()
        self.drop1   = nn.Dropout(dropout)

        self.conv2   = nn.utils.weight_norm(
            nn.Conv1d(n_outputs, n_outputs, kernel_size,
                      stride=stride, padding=padding, dilation=dilation))
        self.chomp2  = Chomp1d(padding)
        self.relu2   = nn.ReLU()
        self.drop2   = nn.Dropout(dropout)

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
    출력: (batch, 3)  → focus / distracted / drowsy
    """
    def __init__(self, input_size=INPUT_DIM, num_classes=3,
                 num_channels=None, kernel_size=KERNEL_SIZE, dropout=DROPOUT):
        super().__init__()
        if num_channels is None:
            num_channels = TCN_CHANNELS

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
        out = self.tcn(x)       # (batch, ch, 150)
        out = out[:, :, -1]     # 마지막 시점 (batch, ch)
        return self.classifier(out)


# ──────────────────────────────────────────
# 5. 학습 / 평가
# ──────────────────────────────────────────

def train_one_epoch(model, loader, optimizer, criterion, device):
    model.train()
    total_loss, correct, total = 0.0, 0, 0

    for X, y in loader:
        X, y = X.to(device), y.to(device)
        optimizer.zero_grad()
        out  = model(X)
        loss = criterion(out, y)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * X.size(0)
        correct    += out.argmax(1).eq(y).sum().item()
        total      += y.size(0)

    return total_loss / total, correct / total


@torch.no_grad()
def evaluate(model, loader, criterion, device):
    model.eval()
    total_loss, correct, total = 0.0, 0, 0
    all_preds, all_labels = [], []

    for X, y in loader:
        X, y  = X.to(device), y.to(device)
        out   = model(X)
        loss  = criterion(out, y)
        preds = out.argmax(1)

        total_loss += loss.item() * X.size(0)
        correct    += preds.eq(y).sum().item()
        total      += y.size(0)
        all_preds.extend(preds.cpu().numpy())
        all_labels.extend(y.cpu().numpy())

    return (total_loss / total, correct / total,
            np.array(all_preds), np.array(all_labels))


# ──────────────────────────────────────────
# 6. 버전 관리
# ──────────────────────────────────────────

def get_next_version(model_dir: Path) -> int:
    if not model_dir.exists():
        return 1
    existing = list(model_dir.glob("tcn_v*.pt"))
    if not existing:
        return 1
    versions = []
    for f in existing:
        try:
            versions.append(int(f.stem.replace("tcn_v", "")))
        except ValueError:
            continue
    return max(versions) + 1 if versions else 1


def list_all_versions(model_dir: Path):
    files = sorted(model_dir.glob("tcn_v*.pt"), key=lambda f: f.stem)
    if not files:
        print("   (저장된 버전 없음)")
        return

    print(f"   {'버전':<6} {'파일명':<25} {'val 정확도':<12} {'test 정확도':<12} {'학습 시각'}")
    print(f"   {'─'*6} {'─'*25} {'─'*12} {'─'*12} {'─'*20}")
    for f in files:
        try:
            d = torch.load(f, map_location="cpu", weights_only=False)
            v        = d.get("version",       "?")
            val_acc  = d.get("val_accuracy",  0.0)
            test_acc = d.get("test_accuracy", 0.0)
            trained  = d.get("trained_at",    "?")[:19]
            stopped  = d.get("stopped_epoch", d.get("hyperparameters", {}).get("num_epochs", "?"))
            marker   = " ⭐" if f.name == files[-1].name else "  "
            print(f"  {marker}v{v:<4} {f.name:<25} "
                  f"{val_acc*100:>8.2f}%    "
                  f"{test_acc*100:>8.2f}%    "
                  f"{trained}  (종료: {stopped}에폭)")
        except Exception:
            print(f"     {f.name}  (읽기 실패)")


# ──────────────────────────────────────────
# 7. 메인
# ──────────────────────────────────────────

def main():
    random.seed(RANDOM_SEED)
    np.random.seed(RANDOM_SEED)
    torch.manual_seed(RANDOM_SEED)
    torch.cuda.manual_seed_all(RANDOM_SEED)

    print("=" * 60)
    print("📚 StudySync TCN 학습 — 3클래스 시계열 분류")
    print("=" * 60)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"\n🖥️  학습 디바이스: {device}")

    # 1. 데이터 로드
    splits = load_all_data()
    X_train, y_train = splits["train"]
    X_val,   y_val   = splits["val"]
    X_test,  y_test  = splits["test"]

    # 2. 정규화
    print("\n🔧 정규화 적용 중...")
    mean, std = fit_scaler(X_train)
    X_train = apply_scaler(X_train, mean, std)
    X_val   = apply_scaler(X_val,   mean, std)
    X_test  = apply_scaler(X_test,  mean, std)

    # 3. DataLoader
    train_loader = DataLoader(
        FocusSequenceDataset(X_train, y_train),
        batch_size=BATCH_SIZE, shuffle=True
    )
    val_loader = DataLoader(
        FocusSequenceDataset(X_val, y_val),
        batch_size=BATCH_SIZE, shuffle=False
    )
    test_loader = DataLoader(
        FocusSequenceDataset(X_test, y_test),
        batch_size=BATCH_SIZE, shuffle=False
    )

    # 4. 모델 / 손실 / 옵티마이저
    model = StudySyncTCN(
        input_size=INPUT_DIM,
        num_classes=3,
        num_channels=TCN_CHANNELS,
        kernel_size=KERNEL_SIZE,
        dropout=DROPOUT,
    ).to(device)

    # FocalLoss + 비대칭 클래스 가중치
    # distracted 오분류에 4배, drowsy 오분류에 2배 페널티
    # Focal Loss: 어려운 샘플(distracted↔drowsy 경계)에 자동 집중
    class_weights = torch.tensor(CLASS_WEIGHTS, dtype=torch.float32).to(device)
    criterion = FocalLoss(weight=class_weights, gamma=2.0)

    # AdamW (L2 정규화 포함, 일반화 향상)
    optimizer = optim.AdamW(model.parameters(), lr=LEARNING_RATE, weight_decay=1e-4)

    # CosineAnnealingLR (부드럽게 학습률 감소)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(
        optimizer, T_max=NUM_EPOCHS, eta_min=1e-6
    )

    print(f"\n🧠 TCN 구조:")
    print(f"   입력: {INPUT_DIM}차원 × {SEQUENCE_LENGTH}프레임")
    print(f"   채널: {TCN_CHANNELS}")
    print(f"   커널: {KERNEL_SIZE}, 드롭아웃: {DROPOUT}")
    print(f"   배치: {BATCH_SIZE}, 학습률: {LEARNING_RATE}, 최대 에폭: {NUM_EPOCHS}")
    print(f"   Early Stopping patience: {PATIENCE}")
    print(f"   클래스 가중치: focus={CLASS_WEIGHTS[0]} / distracted={CLASS_WEIGHTS[1]} / drowsy={CLASS_WEIGHTS[2]}")
    print(f"   손실함수: FocalLoss (gamma=2.0, 어려운 샘플 집중)")
    print(f"   옵티마이저: AdamW (weight_decay=1e-4)")
    print(f"   LR 스케줄러: CosineAnnealingLR (부드럽게 감소)")

    # 5. 학습 (Early Stopping 포함)
    print(f"\n🚀 학습 시작\n")

    best_val_acc = 0.0
    best_state   = None
    no_improve   = 0
    stopped_epoch = NUM_EPOCHS

    for epoch in range(1, NUM_EPOCHS + 1):
        train_loss, train_acc = train_one_epoch(
            model, train_loader, optimizer, criterion, device
        )
        val_loss, val_acc, _, _ = evaluate(
            model, val_loader, criterion, device
        )

        # 학습률 스케줄러 업데이트
        scheduler.step()

        # Best 모델 갱신
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state   = {k: v.cpu().clone()
                            for k, v in model.state_dict().items()}
            no_improve   = 0
        else:
            no_improve += 1

        # 5에폭마다 출력
        if epoch % 5 == 0 or epoch == 1 or epoch == NUM_EPOCHS:
            cur_lr = scheduler.get_last_lr()[0]
            print(f"  Epoch {epoch:3d}/{NUM_EPOCHS}  "
                  f"train loss={train_loss:.4f} acc={train_acc:.4f}  "
                  f"val loss={val_loss:.4f} acc={val_acc:.4f}  "
                  f"lr={cur_lr:.6f}  patience={no_improve}/{PATIENCE}")

        # Early Stopping 조건
        if no_improve >= PATIENCE:
            stopped_epoch = epoch
            print(f"\n  ⏹️  Early Stopping: val 정확도가 {PATIENCE}에폭 동안 "
                  f"향상 없음 → {epoch}에폭에서 조기 종료")
            break

    # 6. 최고 모델 복원 + 테스트 평가
    model.load_state_dict(best_state)
    print(f"\n🏆 최고 검증 정확도: {best_val_acc*100:.2f}%")
    print(f"   실제 학습 에폭: {stopped_epoch}/{NUM_EPOCHS}")

    _, test_acc, preds, labels = evaluate(
        model, test_loader, criterion, device
    )
    print(f"\n📊 테스트 결과:")
    print(f"   테스트 정확도: {test_acc*100:.2f}%\n")

    print("📋 분류 보고서 (Test):")
    print(classification_report(labels, preds,
                                 target_names=LABEL_NAMES, digits=4))

    print("📋 혼동 행렬 (Test):")
    cm = confusion_matrix(labels, preds)
    header = "예측:    " + " ".join(f"{n:>12s}" for n in LABEL_NAMES)
    print(f"   {header}")
    for i, name in enumerate(LABEL_NAMES):
        row = " ".join(f"{cm[i][j]:>12d}" for j in range(len(LABEL_NAMES)))
        print(f"   실제 {name:>10s}: {row}")

    # 7. 모델 저장 (버전 관리)
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    next_version   = get_next_version(MODEL_DIR)
    versioned_name = f"tcn_v{next_version}.pt"
    versioned_path = MODEL_DIR / versioned_name

    save_data = {
        "model_state_dict": model.state_dict(),
        "input_dim":        INPUT_DIM,
        "sequence_length":  SEQUENCE_LENGTH,
        "num_classes":      3,
        "tcn_channels":     TCN_CHANNELS,
        "kernel_size":      KERNEL_SIZE,
        "dropout":          DROPOUT,
        "feature_columns":  FEATURE_COLUMNS,
        "label_map":        LABEL_MAP,
        "label_names":      LABEL_NAMES,
        "scaler_mean":      mean.tolist(),
        "scaler_std":       std.tolist(),
        "val_accuracy":     float(best_val_acc),
        "test_accuracy":    float(test_acc),
        "version":          next_version,
        "stopped_epoch":    stopped_epoch,
        "trained_at":       datetime.now().isoformat(),
        "hyperparameters": {
            "batch_size":     BATCH_SIZE,
            "learning_rate":  LEARNING_RATE,
            "num_epochs":     NUM_EPOCHS,
            "dropout":        DROPOUT,
            "tcn_channels":   TCN_CHANNELS,
            "kernel_size":    KERNEL_SIZE,
            "patience":       PATIENCE,
        },
        "data_split": {
            "train": int(len(X_train)),
            "val":   int(len(X_val)),
            "test":  int(len(X_test)),
        },
    }

    torch.save(save_data, versioned_path)
    shutil.copy(versioned_path, MODEL_PATH)

    print(f"\n💾 모델 저장 완료:")
    print(f"   📌 버전 파일: {versioned_path.name}  "
          f"({versioned_path.stat().st_size / 1024:.1f} KB)")
    print(f"   📌 최신 모델: {MODEL_PATH.name}  (추론 시 사용)")
    print(f"   🏷️  버전:      v{next_version}")
    print(f"   🎯 정확도:    val {best_val_acc*100:.2f}%  /  "
          f"test {test_acc*100:.2f}%")
    print(f"   ⏱️  종료:      {stopped_epoch}에폭")

    print(f"\n📂 저장된 모델 버전 목록:")
    list_all_versions(MODEL_DIR)

    print("\n✅ 학습 완료!")


if __name__ == "__main__":
    main()