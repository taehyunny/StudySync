"""
StudySync TCN용 데이터 수집 스크립트
======================================
MediaPipe Pose + Face Mesh로 좌표를 추출한 뒤,
TCN 입력에 필요한 7개 특징값을 CSV로만 저장합니다.

특징:
    ❌ 이미지 저장 안 함 (CSV만 저장)
    ❌ 좌표 자체 저장 안 함 (가공된 특징값만 저장)
    ✅ Pose + Face Mesh 둘 다 사용

저장되는 7개 특징값 (TCN 입력):
    - ear            (Eye Aspect Ratio, 눈 종횡비)
    - neck_angle     (목 기울기 각도)
    - shoulder_diff  (어깨 좌우 높이 차)
    - head_yaw       (고개 좌우 회전)
    - head_pitch     (고개 앞뒤 끄덕임)
    - face_detected  (얼굴 감지 0/1)
    - phone_detected (핸드폰 감지 0/1, 일단 0 고정)

조작 방법:
    1 → 공부중 (focus) 선택
    2 → 딴짓 (distracted) 선택
    3 → 졸음 (drowsy) 선택
    s → 캡처 시작/정지 토글
    q → 종료

실행 방법:
    pip install opencv-python mediapipe numpy
    python collect_data.py
"""

import cv2
import csv
import time
import math
import numpy as np
import mediapipe as mp
from pathlib import Path

# ──────────────────────────────────────────
# 설정
# ──────────────────────────────────────────

CAPTURE_FPS = 30                          # 30fps (TCN 시계열용)
CAPTURE_INTERVAL = 1.0 / CAPTURE_FPS

# 경로 자동 설정
THIS_FILE     = Path(__file__).resolve()
AI_SERVER_DIR = THIS_FILE.parent
STUDYSYNC_DIR = AI_SERVER_DIR.parent
BASE_DIR      = STUDYSYNC_DIR / "dataset"

# 클래스 정의
CLASSES = {
    "1": "focus",        # 공부중
    "2": "distracted",   # 딴짓
    "3": "drowsy",       # 졸음
}

CLASS_KR = {
    "focus":      "공부중",
    "distracted": "딴짓",
    "drowsy":     "졸음",
}

CLASS_COLOR = {
    "focus":      (0, 200, 100),
    "distracted": (0, 140, 255),
    "drowsy":     (60, 60, 220),
}

# 목표 프레임 수 (UI 진행률 표시용)
# focus 800 시퀀스, drowsy/distracted 400 시퀀스 (1 시퀀스 = 150프레임)
TARGET_FRAMES = {
    "focus":      120000,    # 800 시퀀스 × 150프레임
    "distracted": 60000,     # 400 시퀀스 × 150프레임
    "drowsy":     60000,     # 400 시퀀스 × 150프레임
}


# ──────────────────────────────────────────
# MediaPipe 초기화
# ──────────────────────────────────────────

mp_pose = mp.solutions.pose
mp_face_mesh = mp.solutions.face_mesh

pose_detector = mp_pose.Pose(
    static_image_mode=False,
    model_complexity=1,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5,
)

face_mesh = mp_face_mesh.FaceMesh(
    static_image_mode=False,
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5,
)

# Face Mesh 랜드마크 인덱스 (EAR 계산용)
LEFT_EYE_IDX  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE_IDX = [362, 385, 387, 263, 373, 380]


# ──────────────────────────────────────────
# 특징값 추출 (Pose + Face Mesh)
# ──────────────────────────────────────────

def extract_features(frame: np.ndarray) -> dict | None:
    """
    한 프레임에서 TCN 입력용 7개 특징값 추출
    반환: dict 또는 None (얼굴 미감지 시)
    """
    h, w = frame.shape[:2]
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    pose_result = pose_detector.process(rgb)
    face_result = face_mesh.process(rgb)

    if not face_result.multi_face_landmarks:
        return None

    flm = face_result.multi_face_landmarks[0].landmark
    face_detected = 1

    # ── neck_angle, shoulder_diff (Pose 사용)
    neck_angle    = 0.0
    shoulder_diff = 0.0

    if pose_result.pose_landmarks:
        lm = pose_result.pose_landmarks.landmark

        # 귀(7) ~ 어깨(11) 의 수직선 대비 각도
        ear_x = lm[7].x  * w
        ear_y = lm[7].y  * h
        sh_x  = lm[11].x * w
        sh_y  = lm[11].y * h

        dx = abs(ear_x - sh_x)
        dy = abs(ear_y - sh_y)
        neck_angle = float(np.degrees(np.arctan2(dx, dy)))

        # 양쪽 어깨 y좌표 차이 (픽셀)
        shoulder_diff = abs(lm[11].y - lm[12].y) * h

    # ── EAR 계산 (Face Mesh)
    def calc_ear(eye_idx):
        pts = [(flm[i].x, flm[i].y) for i in eye_idx]
        v1 = math.dist(pts[1], pts[5])
        v2 = math.dist(pts[2], pts[4])
        h_ = math.dist(pts[0], pts[3])
        return (v1 + v2) / (2.0 * h_) if h_ > 0 else 0.0

    ear = (calc_ear(LEFT_EYE_IDX) + calc_ear(RIGHT_EYE_IDX)) / 2.0

    # ── head_pitch (코끝 1 ~ 턱 152)
    head_pitch = float(np.degrees(
        np.arctan2(flm[152].y - flm[1].y, flm[152].x - flm[1].x)
    )) - 90

    # ── head_yaw (좌우 얼굴 가장자리 454, 234)
    head_yaw = float((flm[454].x - flm[234].x) * 100)

    # ── phone_detected (YOLO 미사용 시 0 고정)
    phone_detected = 0

    return {
        "ear":            round(ear, 4),
        "neck_angle":     round(neck_angle, 2),
        "shoulder_diff":  round(shoulder_diff, 2),
        "head_yaw":       round(head_yaw, 2),
        "head_pitch":     round(head_pitch, 2),
        "face_detected":  face_detected,
        "phone_detected": phone_detected,
    }


# ──────────────────────────────────────────
# CSV 관리
# ──────────────────────────────────────────

CSV_HEADERS = [
    "timestamp_ms",
    "ear",
    "neck_angle",
    "shoulder_diff",
    "head_yaw",
    "head_pitch",
    "face_detected",
    "phone_detected",
    "label",
]


def get_csv_path(class_name: str) -> Path:
    return BASE_DIR / class_name / "labels.csv"


def init_csv_for_class(class_name: str):
    """CSV 파일 없으면 헤더와 함께 생성"""
    csv_path = get_csv_path(class_name)
    if csv_path.exists():
        with open(csv_path, "r", encoding="utf-8") as f:
            row_count = sum(1 for _ in f) - 1
        print(f"   └─ {class_name}/labels.csv: 기존 {row_count}개 프레임")
        return

    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        csv.writer(f).writerow(CSV_HEADERS)
    print(f"   └─ {class_name}/labels.csv: 새로 생성")


def init_all_csvs():
    print("📋 클래스별 labels.csv 확인:")
    BASE_DIR.mkdir(parents=True, exist_ok=True)
    for cls in CLASSES.values():
        init_csv_for_class(cls)
    print()


def append_features(features: dict, label: str):
    """특징값 1행을 해당 클래스 CSV에 추가"""
    csv_path = get_csv_path(label)
    timestamp_ms = int(time.time() * 1000)
    row = [
        timestamp_ms,
        features["ear"],
        features["neck_angle"],
        features["shoulder_diff"],
        features["head_yaw"],
        features["head_pitch"],
        features["face_detected"],
        features["phone_detected"],
        label,
    ]
    with open(csv_path, "a", encoding="utf-8", newline="") as f:
        csv.writer(f).writerow(row)


def count_csv_rows(class_name: str) -> int:
    csv_path = get_csv_path(class_name)
    if not csv_path.exists():
        return 0
    with open(csv_path, "r", encoding="utf-8") as f:
        return sum(1 for _ in f) - 1


# ──────────────────────────────────────────
# UI 렌더링
# ──────────────────────────────────────────

def draw_ui(frame, current_class, is_capturing, counts, face_ok, fps):
    h, w = frame.shape[:2]

    # 상단 박스
    cv2.rectangle(frame, (0, 0), (w, 90), (20, 20, 20), -1)

    if current_class is None:
        cv2.putText(frame, "Select class: [1] focus  [2] distracted  [3] drowsy",
                    (15, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (200, 200, 200), 1)
    else:
        color = CLASS_COLOR[current_class]
        label = CLASS_KR[current_class]
        if is_capturing:
            if int(time.time() * 2) % 2 == 0:
                cv2.circle(frame, (20, 25), 8, (0, 0, 255), -1)
            cv2.putText(frame, f"REC  [{label}] capturing... [s] stop   FPS: {fps:.1f}",
                        (38, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 2)
        else:
            cv2.putText(frame, f"[{label}] selected. press [s] to start",
                        (15, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

    cv2.putText(frame, "[1] focus  [2] distracted  [3] drowsy  [s] start/stop  [q] quit",
                (15, 62), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (150, 150, 150), 1)

    # 얼굴 감지 상태
    face_color = (0, 200, 100) if face_ok else (0, 0, 220)
    face_text  = "Face OK" if face_ok else "Face MISSING"
    cv2.putText(frame, face_text, (w - 180, 32),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, face_color, 2)

    # 하단: 클래스별 진행률
    cv2.rectangle(frame, (0, h - 60), (w, h), (20, 20, 20), -1)
    x_offset = 15
    for cls in ["focus", "distracted", "drowsy"]:
        count  = counts.get(cls, 0)
        target = TARGET_FRAMES[cls]
        color  = CLASS_COLOR[cls]
        label  = CLASS_KR[cls]

        cv2.putText(frame, f"{label}: {count}/{target}f", (x_offset, h - 35),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1)

        bar_w  = 150
        filled = int(min(count / target, 1.0) * bar_w)
        cv2.rectangle(frame, (x_offset, h - 22),
                      (x_offset + bar_w, h - 10), (60, 60, 60), -1)
        cv2.rectangle(frame, (x_offset, h - 22),
                      (x_offset + filled, h - 10), color, -1)
        cv2.putText(frame, f"{int(count/target*100)}%",
                    (x_offset + bar_w + 5, h - 11),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.38, (150, 150, 150), 1)
        x_offset += 220

    # 캡처 중 테두리
    if is_capturing and current_class:
        color = CLASS_COLOR[current_class]
        cv2.rectangle(frame, (0, 0), (w - 1, h - 1), color, 3)

    return frame


# ──────────────────────────────────────────
# 메인
# ──────────────────────────────────────────

def main():
    init_all_csvs()

    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("❌ 웹캠을 열 수 없습니다.")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    cap.set(cv2.CAP_PROP_FPS, CAPTURE_FPS)

    print(f"🎥 웹캠 시작 ({CAPTURE_FPS}fps 목표)")
    print("  [1] 공부중  [2] 딴짓  [3] 졸음 으로 클래스 선택")
    print("  [s] 캡처 시작/정지,  [q] 종료")
    print("  💡 얼굴이 감지되어야 데이터가 저장됩니다.\n")

    current_class    = None
    is_capturing     = False
    last_capture_t   = 0.0
    skipped_count    = 0
    captured_count   = 0

    fps_window = []
    fps_now    = 0.0

    while True:
        ret, frame = cap.read()
        if not ret:
            print("프레임 읽기 실패")
            break

        frame  = cv2.flip(frame, 1)
        counts = {cls: count_csv_rows(cls) for cls in CLASSES.values()}

        face_ok = False

        if is_capturing and current_class is not None:
            now = time.time()
            if now - last_capture_t >= CAPTURE_INTERVAL:
                last_capture_t = now

                features = extract_features(frame)
                face_ok  = features is not None

                if features is not None:
                    append_features(features, current_class)
                    captured_count += 1

                    fps_window.append(now)
                    fps_window = [t for t in fps_window if now - t <= 1.0]
                    fps_now = len(fps_window)

                    # 30프레임마다 한 번씩 콘솔 출력
                    if captured_count % 30 == 0:
                        total = counts[current_class] + 1
                        print(f"  📊 [{CLASS_KR[current_class]}] "
                              f"{total} frames  "
                              f"EAR={features['ear']:.3f} "
                              f"neck={features['neck_angle']:+.1f}° "
                              f"yaw={features['head_yaw']:+.1f}°")
                else:
                    skipped_count += 1
                    if skipped_count % 30 == 1:
                        print(f"  ⚠️  얼굴 미감지 → 스킵 (총 {skipped_count}회)")
        else:
            # 캡처 안 할 때도 얼굴 감지만 빠르게 (UI 표시용)
            face_check = face_mesh.process(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
            face_ok = face_check.multi_face_landmarks is not None

        # UI 표시
        display = draw_ui(frame.copy(), current_class, is_capturing, counts, face_ok, fps_now)
        cv2.imshow("StudySync TCN Data Collect", display)

        # 키 입력 처리
        key = cv2.waitKey(1) & 0xFF

        if key == ord("q"):
            break
        elif key == ord("1"):
            current_class = "focus"
            is_capturing = False
            print(f"▶ 클래스 선택: {CLASS_KR['focus']}")
        elif key == ord("2"):
            current_class = "distracted"
            is_capturing = False
            print(f"▶ 클래스 선택: {CLASS_KR['distracted']}")
        elif key == ord("3"):
            current_class = "drowsy"
            is_capturing = False
            print(f"▶ 클래스 선택: {CLASS_KR['drowsy']}")
        elif key == ord("s"):
            if current_class is None:
                print("⚠️  먼저 클래스를 선택하세요 (1/2/3)")
            else:
                is_capturing = not is_capturing
                state = "시작" if is_capturing else "정지"
                print(f"{'▶' if is_capturing else '⏸'} 캡처 {state}: {CLASS_KR[current_class]}")

    # 종료 시 최종 현황 출력
    print("\n📊 최종 수집 현황:")
    for cls in ["focus", "distracted", "drowsy"]:
        count  = count_csv_rows(cls)
        target = TARGET_FRAMES[cls]
        pct    = int(min(count / target, 1.0) * 100)
        bar    = "█" * (pct // 5) + "░" * (20 - pct // 5)
        print(f"  {CLASS_KR[cls]:5s}: {count:>6d}/{target} frames  [{bar}] {pct}%")

    if skipped_count > 0:
        print(f"\n⚠️  얼굴 미감지로 스킵된 프레임: {skipped_count}개")

    print(f"\n💾 저장 위치: {BASE_DIR}")
    print(f"   ├─ focus/labels.csv")
    print(f"   ├─ distracted/labels.csv")
    print(f"   └─ drowsy/labels.csv")

    cap.release()
    cv2.destroyAllWindows()
    pose_detector.close()
    face_mesh.close()


if __name__ == "__main__":
    main()