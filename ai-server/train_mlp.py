"""
StudySync MLP 학습 스크립트 (Phase 2)
======================================
폴더 구조:
    dataset/drowsy/labels.csv    → 졸음 (label=1)
    dataset/focused/labels.csv   → 집중 (label=0)

학습 흐름:
    1. CSV 로드 (drowsy + focused)
    2. 좌표 51개 + 각도 5개 = 56차원 입력
    3. 데이터 증강 (좌우반전, 노이즈)
    4. PyTorch MLP 학습
    5. 정확도 측정 + 혼동 행렬
    6. focus_classifier.pt 저장

실행 방법:
    pip install torch scikit-learn pandas
    python train_mlp.py
"""

import csv
import math
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
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix


# ──────────────────────────────────────────
# 설정
# ──────────────────────────────────────────

# 경로 자동 설정
THIS_FILE     = Path(__file__).resolve()
AI_SERVER_DIR = THIS_FILE.parent
STUDYSYNC_DIR = AI_SERVER_DIR.parent
BASE_DIR      = STUDYSYNC_DIR / "dataset"

DROWSY_CSV   = BASE_DIR / "drowsy" / "labels.csv"
FOCUSED_CSV  = BASE_DIR / "focused" / "labels.csv"
MODEL_DIR    = AI_SERVER_DIR / "models"
MODEL_PATH   = MODEL_DIR / "focus_classifier.pt"

# 라벨 매핑 (이진 분류)
LABEL_MAP = {
    "focused": 0,    # 집중
    "drowsy":  1,    # 졸음
}
LABEL_NAMES = ["focused", "drowsy"]

# 학습 하이퍼파라미터
RANDOM_SEED   = 42
TEST_SIZE     = 0.2
BATCH_SIZE    = 32
LEARNING_RATE = 0.001
NUM_EPOCHS    = 100
HIDDEN_DIM_1  = 128
HIDDEN_DIM_2  = 64
DROPOUT       = 0.3

# 데이터 증강 배수
AUGMENT_FACTOR = 4    # 원본 1개 → 증강 후 4개


# ──────────────────────────────────────────
# 1. CSV 로드
# ──────────────────────────────────────────

def load_csv(csv_path: Path, label_name: str) -> pd.DataFrame:
    """클래스별 CSV 로드 후 라벨 매핑"""
    if not csv_path.exists():
        raise FileNotFoundError(f"❌ CSV 파일 없음: {csv_path}")

    df = pd.read_csv(csv_path)
    print(f"  📄 {label_name}: {len(df)}개 행 로드 ({csv_path.name})")
    return df


def load_all_data() -> tuple[np.ndarray, np.ndarray]:
    """모든 CSV 로드 후 X (특징), y (라벨) 반환"""
    print("\n📂 데이터셋 로드 중...")

    df_focused = load_csv(FOCUSED_CSV, "집중")
    df_drowsy  = load_csv(DROWSY_CSV,  "졸음")

    df_focused["label_int"] = LABEL_MAP["focused"]
    df_drowsy["label_int"]  = LABEL_MAP["drowsy"]

    df_all = pd.concat([df_focused, df_drowsy], ignore_index=True)

    # 사용할 특징 컬럼 자동 선택 (filename, label, label_int 제외)
    feature_cols = [c for c in df_all.columns
                    if c not in ("filename", "label", "label_int")]

    X = df_all[feature_cols].values.astype(np.float32)
    y = df_all["label_int"].values.astype(np.int64)

    print(f"\n✅ 총 샘플 수: {len(X)}개")
    print(f"   입력 차원: {X.shape[1]}차원")
    print(f"   클래스 분포:")
    print(f"     - 집중(0): {np.sum(y == 0)}개")
    print(f"     - 졸음(1): {np.sum(y == 1)}개")

    return X, y, feature_cols


# ──────────────────────────────────────────
# 2. 데이터 증강
# ──────────────────────────────────────────

def augment_features(X: np.ndarray, y: np.ndarray, factor: int = 4) -> tuple[np.ndarray, np.ndarray]:
    """
    좌표 + 각도 증강 (원본 1개 → factor개)

    증강 방법:
      1. 원본
      2. 좌우 반전 (x 좌표 1.0 - x, head_yaw 부호 반전)
      3. 노이즈 추가 (좌표에 ±0.005)
      4. 스케일 변환 (좌표 ×0.97~1.03)
    """
    print("\n🔄 데이터 증강 중...")

    augmented_X = []
    augmented_y = []

    for i in range(len(X)):
        original = X[i].copy()
        label = y[i]

        # 원본
        augmented_X.append(original)
        augmented_y.append(label)

        if factor >= 2:
            # 좌우 반전 (x 좌표만 1.0 - x)
            flipped = original.copy()
            for j in range(0, 51, 3):    # 17개 랜드마크 × (x, y, z)
                flipped[j] = 1.0 - flipped[j]
            # head_yaw, head_roll 부호 반전 (각도 컬럼 위치)
            # 컬럼 순서: lm0_x, ..., lm16_z, ear, mar, head_yaw, head_pitch, head_roll
            if len(original) >= 56:
                flipped[53] = -flipped[53]   # head_yaw
                flipped[55] = -flipped[55]   # head_roll
            augmented_X.append(flipped)
            augmented_y.append(label)

        if factor >= 3:
            # 노이즈 추가
            noise = np.random.normal(0, 0.005, original.shape).astype(np.float32)
            noisy = original + noise
            augmented_X.append(noisy)
            augmented_y.append(label)

        if factor >= 4:
            # 스케일 변환
            scale = np.random.uniform(0.97, 1.03)
            scaled = original.copy()
            for j in range(0, 51):
                scaled[j] *= scale
            augmented_X.append(scaled)
            augmented_y.append(label)

    X_aug = np.array(augmented_X, dtype=np.float32)
    y_aug = np.array(augmented_y, dtype=np.int64)

    print(f"  원본: {len(X)}개 → 증강 후: {len(X_aug)}개 ({factor}배)")
    return X_aug, y_aug


# ──────────────────────────────────────────
# 3. PyTorch Dataset
# ──────────────────────────────────────────

class FocusDataset(Dataset):
    def __init__(self, X: np.ndarray, y: np.ndarray):
        self.X = torch.tensor(X, dtype=torch.float32)
        self.y = torch.tensor(y, dtype=torch.long)

    def __len__(self):
        return len(self.X)

    def __getitem__(self, idx):
        return self.X[idx], self.y[idx]


# ──────────────────────────────────────────
# 4. MLP 모델
# ──────────────────────────────────────────

class FocusMLP(nn.Module):
    """
    입력: 56차원 (좌표 51 + ear, mar, yaw, pitch, roll)
    출력: 2차원 (focused / drowsy)
    """

    def __init__(self, input_dim: int = 56, hidden_dim_1: int = 128,
                 hidden_dim_2: int = 64, num_classes: int = 2, dropout: float = 0.3):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(input_dim, hidden_dim_1),
            nn.ReLU(),
            nn.Dropout(dropout),

            nn.Linear(hidden_dim_1, hidden_dim_2),
            nn.ReLU(),
            nn.Dropout(dropout),

            nn.Linear(hidden_dim_2, num_classes),
        )

    def forward(self, x):
        return self.layers(x)


# ──────────────────────────────────────────
# 5. 학습 / 평가 함수
# ──────────────────────────────────────────

def train_one_epoch(model, loader, optimizer, criterion, device) -> tuple[float, float]:
    model.train()
    total_loss = 0.0
    correct = 0
    total = 0

    for X_batch, y_batch in loader:
        X_batch, y_batch = X_batch.to(device), y_batch.to(device)

        optimizer.zero_grad()
        outputs = model(X_batch)
        loss = criterion(outputs, y_batch)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * X_batch.size(0)
        _, predicted = outputs.max(1)
        correct += predicted.eq(y_batch).sum().item()
        total += y_batch.size(0)

    avg_loss = total_loss / total
    accuracy = correct / total
    return avg_loss, accuracy


@torch.no_grad()
def evaluate(model, loader, criterion, device) -> tuple[float, float, np.ndarray, np.ndarray]:
    model.eval()
    total_loss = 0.0
    correct = 0
    total = 0
    all_preds = []
    all_labels = []

    for X_batch, y_batch in loader:
        X_batch, y_batch = X_batch.to(device), y_batch.to(device)
        outputs = model(X_batch)
        loss = criterion(outputs, y_batch)

        total_loss += loss.item() * X_batch.size(0)
        _, predicted = outputs.max(1)
        correct += predicted.eq(y_batch).sum().item()
        total += y_batch.size(0)

        all_preds.extend(predicted.cpu().numpy())
        all_labels.extend(y_batch.cpu().numpy())

    avg_loss = total_loss / total
    accuracy = correct / total
    return avg_loss, accuracy, np.array(all_preds), np.array(all_labels)



# ──────────────────────────────────────────
# 버전 관리 헬퍼
# ──────────────────────────────────────────

def get_next_version(model_dir: Path) -> int:
    """models 폴더에서 가장 큰 버전 번호 + 1 반환"""
    if not model_dir.exists():
        return 1

    pattern = "focus_classifier_v*.pt"
    existing = list(model_dir.glob(pattern))

    if not existing:
        return 1

    # 파일명에서 버전 번호 추출 (focus_classifier_v3.pt → 3)
    versions = []
    for f in existing:
        try:
            v = int(f.stem.replace("focus_classifier_v", ""))
            versions.append(v)
        except ValueError:
            continue

    return max(versions) + 1 if versions else 1


def list_all_versions(model_dir: Path):
    """저장된 모든 버전 모델을 정확도순으로 출력"""
    pattern = "focus_classifier_v*.pt"
    files = sorted(model_dir.glob(pattern), key=lambda f: f.stem)

    if not files:
        print("   (저장된 버전 없음)")
        return

    versions_info = []
    for f in files:
        try:
            data = torch.load(f, map_location="cpu", weights_only=False)
            acc = data.get("accuracy", 0.0)
            trained_at = data.get("trained_at", "?")[:19]
            version = data.get("version", "?")
            versions_info.append((version, f.name, acc, trained_at))
        except Exception:
            versions_info.append(("?", f.name, 0.0, "?"))

    # 출력
    print(f"   {'버전':<6} {'파일명':<30} {'정확도':<10} {'학습 시각':<20}")
    print(f"   {'─'*6} {'─'*30} {'─'*10} {'─'*20}")
    for version, name, acc, trained in versions_info:
        marker = " ⭐" if name == files[-1].name else "  "
        print(f"  {marker}v{version:<4} {name:<30} {acc*100:>6.2f}%   {trained}")


# ──────────────────────────────────────────
# 6. 메인 학습 흐름
# ──────────────────────────────────────────

def main():
    # 시드 고정 (재현성)
    random.seed(RANDOM_SEED)
    np.random.seed(RANDOM_SEED)
    torch.manual_seed(RANDOM_SEED)

    print("=" * 60)
    print("📚 StudySync MLP 학습 — 졸음/집중 이진 분류")
    print("=" * 60)

    # GPU 사용 가능 여부
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"\n🖥️  학습 디바이스: {device}")

    # 1. 데이터 로드
    X, y, feature_cols = load_all_data()

    # 2. 학습/검증 분할 (8:2)
    X_train, X_test, y_train, y_test = train_test_split(
        X, y,
        test_size=TEST_SIZE,
        random_state=RANDOM_SEED,
        stratify=y,
    )
    print(f"\n📊 데이터 분할:")
    print(f"   - 학습: {len(X_train)}개")
    print(f"   - 검증: {len(X_test)}개")

    # 3. 학습 데이터만 증강 (검증 데이터는 원본 그대로)
    X_train, y_train = augment_features(X_train, y_train, factor=AUGMENT_FACTOR)

    # 4. DataLoader 생성
    train_dataset = FocusDataset(X_train, y_train)
    test_dataset  = FocusDataset(X_test,  y_test)

    train_loader = DataLoader(train_dataset, batch_size=BATCH_SIZE, shuffle=True)
    test_loader  = DataLoader(test_dataset,  batch_size=BATCH_SIZE, shuffle=False)

    # 5. 모델 / 손실 / 옵티마이저 초기화
    input_dim = X.shape[1]
    model = FocusMLP(
        input_dim=input_dim,
        hidden_dim_1=HIDDEN_DIM_1,
        hidden_dim_2=HIDDEN_DIM_2,
        num_classes=2,
        dropout=DROPOUT,
    ).to(device)

    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=LEARNING_RATE)

    print(f"\n🧠 모델 구조:")
    print(f"   입력 {input_dim} → 은닉1 {HIDDEN_DIM_1} → 은닉2 {HIDDEN_DIM_2} → 출력 2")
    print(f"   에폭: {NUM_EPOCHS},  배치: {BATCH_SIZE},  학습률: {LEARNING_RATE}")

    # 6. 학습 루프
    print(f"\n🚀 학습 시작\n")
    best_acc = 0.0
    best_state = None

    for epoch in range(1, NUM_EPOCHS + 1):
        train_loss, train_acc = train_one_epoch(model, train_loader, optimizer, criterion, device)
        val_loss, val_acc, _, _ = evaluate(model, test_loader, criterion, device)

        if val_acc > best_acc:
            best_acc = val_acc
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}

        # 10 에폭마다 출력
        if epoch % 10 == 0 or epoch == 1 or epoch == NUM_EPOCHS:
            print(f"  Epoch {epoch:3d}/{NUM_EPOCHS}  "
                  f"학습 loss={train_loss:.4f} acc={train_acc:.4f}  "
                  f"검증 loss={val_loss:.4f} acc={val_acc:.4f}")

    # 7. 최고 성능 모델 복원
    model.load_state_dict(best_state)
    print(f"\n🏆 최고 검증 정확도: {best_acc:.4f} ({best_acc*100:.1f}%)")

    # 8. 최종 평가 (혼동 행렬, 분류 보고서)
    _, final_acc, preds, labels = evaluate(model, test_loader, criterion, device)
    print(f"\n📊 최종 평가 결과:")
    print(f"   검증 정확도: {final_acc:.4f} ({final_acc*100:.1f}%)\n")

    print("📋 분류 보고서:")
    print(classification_report(labels, preds, target_names=LABEL_NAMES, digits=4))

    print("📋 혼동 행렬:")
    cm = confusion_matrix(labels, preds)
    print(f"           예측: {LABEL_NAMES[0]:>10s} {LABEL_NAMES[1]:>10s}")
    for i, name in enumerate(LABEL_NAMES):
        print(f"   실제 {name:>8s}: {cm[i][0]:>10d} {cm[i][1]:>10d}")

    # 9. 모델 저장 (버전 자동 관리)
    MODEL_DIR.mkdir(parents=True, exist_ok=True)

    # 다음 버전 번호 자동 결정 (v1, v2, v3, ...)
    next_version = get_next_version(MODEL_DIR)
    versioned_name = f"focus_classifier_v{next_version}.pt"
    versioned_path = MODEL_DIR / versioned_name

    # 추론 시 필요한 메타데이터까지 저장
    save_data = {
        "model_state_dict": model.state_dict(),
        "input_dim":        input_dim,
        "hidden_dim_1":     HIDDEN_DIM_1,
        "hidden_dim_2":     HIDDEN_DIM_2,
        "num_classes":      2,
        "dropout":          DROPOUT,
        "feature_columns":  feature_cols,
        "label_map":        LABEL_MAP,
        "label_names":      LABEL_NAMES,
        "accuracy":         float(best_acc),
        "version":          next_version,
        "trained_at":       datetime.now().isoformat(),
        "hyperparameters": {
            "batch_size":     BATCH_SIZE,
            "learning_rate":  LEARNING_RATE,
            "num_epochs":     NUM_EPOCHS,
            "hidden_dim_1":   HIDDEN_DIM_1,
            "hidden_dim_2":   HIDDEN_DIM_2,
            "dropout":        DROPOUT,
            "augment_factor": AUGMENT_FACTOR,
        },
    }

    # 1. 버전별 파일 저장 (v1, v2, ...)
    torch.save(save_data, versioned_path)

    # 2. focus_classifier.pt 는 항상 "최신 버전"으로 복사 (추론용 고정 경로)
    shutil.copy(versioned_path, MODEL_PATH)

    print(f"\n💾 모델 저장 완료:")
    print(f"   📌 버전 파일:  {versioned_path.name}  ({versioned_path.stat().st_size / 1024:.1f} KB)")
    print(f"   📌 최신 모델:  {MODEL_PATH.name}  (추론 시 사용)")
    print(f"   🏷️  버전:       v{next_version}")
    print(f"   🎯 정확도:     {best_acc*100:.2f}%")

    # 3. 모든 버전 요약 출력
    print(f"\n📂 저장된 모델 버전 목록:")
    list_all_versions(MODEL_DIR)

    print("\n✅ 학습 완료!")


if __name__ == "__main__":
    main()