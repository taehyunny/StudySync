"""
StudySync 기존 이미지 재처리 스크립트
--------------------------------------
이미 수집한 이미지들을 다시 읽어서 좌표 + 각도가 포함된
새 형식의 labels.csv를 생성합니다.

기존 이미지를 다시 찍을 필요 없음!

실행 방법:
    python regenerate_csv.py

처리 흐름:
    dataset/focused/*.jpg     → focused/labels.csv
    dataset/distracted/*.jpg  → distracted/labels.csv
    dataset/drowsy/*.jpg      → drowsy/labels.csv

각 클래스별로 새 CSV가 생성되며 기존 CSV는 자동으로 백업됩니다.
"""

import cv2
import csv
import math
import shutil
import numpy as np
import mediapipe as mp
from pathlib import Path
from datetime import datetime

# ──────────────────────────────────────────
# 경로 설정 (collect_data.py와 동일)
# ──────────────────────────────────────────

THIS_FILE     = Path(__file__).resolve()
AI_SERVER_DIR = THIS_FILE.parent
STUDYSYNC_DIR = AI_SERVER_DIR.parent
BASE_DIR      = STUDYSYNC_DIR / "dataset"

CLASSES = ["focused", "distracted", "drowsy"]
CLASS_KR = {
    "focused":    "공부중",
    "distracted": "딴짓",
    "drowsy":     "졸음",
}

# ──────────────────────────────────────────
# MediaPipe Face Mesh
# ──────────────────────────────────────────

mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    static_image_mode=True,             # 정적 이미지 모드
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.5,
)

# 사용할 랜드마크 인덱스
LEFT_EYE_IDX  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE_IDX = [362, 385, 387, 263, 373, 380]
NOSE_IDX      = [1]
MOUTH_IDX     = [13, 14, 78, 308]
USED_INDICES  = LEFT_EYE_IDX + RIGHT_EYE_IDX + NOSE_IDX + MOUTH_IDX  # 17개


# ──────────────────────────────────────────
# 좌표 + 각도 추출 (collect_data.py와 동일 로직)
# ──────────────────────────────────────────

def extract_features(image_path: Path) -> tuple[list[float], dict] | None:
    """이미지 1장 → 좌표 51개 + 특징값 dict"""
    # 한글 경로 호환 - imread 대신 imdecode 사용
    img_array = np.fromfile(str(image_path), dtype=np.uint8)
    frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

    if frame is None:
        return None

    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = face_mesh.process(rgb)

    if not results.multi_face_landmarks:
        return None

    lm = results.multi_face_landmarks[0].landmark

    # 17개 랜드마크 × (x, y, z) = 51개 값
    coords = []
    for idx in USED_INDICES:
        coords.extend([lm[idx].x, lm[idx].y, lm[idx].z])

    # EAR
    def calc_ear(eye_idx):
        pts = [(lm[i].x, lm[i].y) for i in eye_idx]
        v1 = math.dist(pts[1], pts[5])
        v2 = math.dist(pts[2], pts[4])
        h  = math.dist(pts[0], pts[3])
        return (v1 + v2) / (2.0 * h) if h > 0 else 0.0

    ear = (calc_ear(LEFT_EYE_IDX) + calc_ear(RIGHT_EYE_IDX)) / 2

    # MAR
    mouth_top   = (lm[13].x, lm[13].y)
    mouth_bot   = (lm[14].x, lm[14].y)
    mouth_left  = (lm[78].x, lm[78].y)
    mouth_right = (lm[308].x, lm[308].y)
    mouth_v = math.dist(mouth_top, mouth_bot)
    mouth_h = math.dist(mouth_left, mouth_right)
    mar = mouth_v / mouth_h if mouth_h > 0 else 0.0

    # 머리 각도
    left_eye_center  = (lm[33].x,  lm[33].y,  lm[33].z)
    right_eye_center = (lm[263].x, lm[263].y, lm[263].z)
    nose_tip         = (lm[1].x,   lm[1].y,   lm[1].z)
    mouth_center_y   = (lm[13].y + lm[14].y) / 2

    head_yaw = math.degrees(math.atan2(
        right_eye_center[2] - left_eye_center[2],
        right_eye_center[0] - left_eye_center[0]
    ))

    eye_mid_y = (left_eye_center[1] + right_eye_center[1]) / 2
    nose_eye_dy = nose_tip[1] - eye_mid_y
    nose_mouth_dy = mouth_center_y - nose_tip[1]
    pitch_ratio = nose_eye_dy / (nose_mouth_dy + 1e-6)
    head_pitch = math.degrees(math.atan(pitch_ratio - 1.0))

    head_roll = math.degrees(math.atan2(
        right_eye_center[1] - left_eye_center[1],
        right_eye_center[0] - left_eye_center[0]
    ))

    features = {
        "ear":        round(ear, 4),
        "mar":        round(mar, 4),
        "head_yaw":   round(head_yaw, 2),
        "head_pitch": round(head_pitch, 2),
        "head_roll":  round(head_roll, 2),
    }

    return coords, features


# ──────────────────────────────────────────
# CSV 헤더 생성
# ──────────────────────────────────────────

def make_headers() -> list[str]:
    headers = ["filename"]
    for i in range(len(USED_INDICES)):
        headers.extend([f"lm{i}_x", f"lm{i}_y", f"lm{i}_z"])
    headers.extend(["ear", "mar", "head_yaw", "head_pitch", "head_roll", "label"])
    return headers


# ──────────────────────────────────────────
# 클래스별 재처리
# ──────────────────────────────────────────

def process_class(class_name: str) -> tuple[int, int]:
    """
    한 클래스의 모든 이미지 재처리
    반환: (성공 개수, 실패 개수)
    """
    folder = BASE_DIR / class_name
    csv_path = folder / "labels.csv"

    if not folder.exists():
        print(f"  ⚠️  {class_name}/ 폴더 없음, 스킵")
        return 0, 0

    images = sorted(folder.glob("*.jpg"))
    if not images:
        print(f"  ⚠️  {class_name}/ 폴더에 이미지 없음")
        return 0, 0

    # 기존 CSV 백업
    if csv_path.exists():
        backup_name = f"labels_backup_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        backup_path = folder / backup_name
        shutil.copy(csv_path, backup_path)
        print(f"  📦 기존 CSV 백업: {backup_name}")

    # 새 CSV 작성
    headers = make_headers()
    success = 0
    failed = 0

    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(headers)

        for i, img_path in enumerate(images, 1):
            result = extract_features(img_path)

            if result is None:
                failed += 1
                if failed <= 5:    # 처음 5개만 출력
                    print(f"     ⚠️  얼굴 미감지: {img_path.name}")
                continue

            coords, features = result
            row = [
                img_path.name,
                *coords,
                features["ear"],
                features["mar"],
                features["head_yaw"],
                features["head_pitch"],
                features["head_roll"],
                class_name,
            ]
            writer.writerow(row)
            success += 1

            # 진행률 표시 (10% 단위)
            if i % max(1, len(images) // 10) == 0:
                pct = i / len(images) * 100
                print(f"     진행 중... {i}/{len(images)} ({pct:.0f}%)")

    return success, failed


# ──────────────────────────────────────────
# 메인
# ──────────────────────────────────────────

def main():
    print("=" * 60)
    print("📋 StudySync 기존 이미지 재처리")
    print("=" * 60)

    if not BASE_DIR.exists():
        print(f"❌ dataset 폴더가 없습니다: {BASE_DIR}")
        print("   먼저 collect_data.py로 데이터를 수집해주세요.")
        return

    print(f"\n📂 기준 경로: {BASE_DIR}\n")

    total_success = 0
    total_failed = 0

    for class_name in CLASSES:
        print(f"🔄 {CLASS_KR[class_name]} ({class_name}) 처리 중...")
        success, failed = process_class(class_name)
        total_success += success
        total_failed += failed
        print(f"  ✅ 성공: {success}장,  ❌ 실패: {failed}장\n")

    # 요약
    print("=" * 60)
    print("📊 재처리 결과 요약")
    print("=" * 60)
    print(f"  성공: {total_success}장")
    print(f"  실패: {total_failed}장 (얼굴 미감지)")

    # 클래스별 최종 라벨 수
    print(f"\n📋 각 클래스 labels.csv 최종 라벨 수:")
    for class_name in CLASSES:
        csv_path = BASE_DIR / class_name / "labels.csv"
        if csv_path.exists():
            with open(csv_path, "r", encoding="utf-8") as f:
                row_count = sum(1 for _ in f) - 1
            print(f"   └─ {class_name}/labels.csv: {row_count}개")

    print(f"\n💡 새 CSV에는 좌표 51개 + 각도 5개(ear, mar, yaw, pitch, roll)가 포함됩니다.")

    face_mesh.close()


if __name__ == "__main__":
    main()