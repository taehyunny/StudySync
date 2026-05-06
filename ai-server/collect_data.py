"""
StudySync 데이터 수집 스크립트
--------------------------------
웹캠으로 공부중 / 딴짓 / 졸음 이미지를 2fps로 촬영하여
position/StudySync/dataset/ 폴더에 자동 저장합니다.

조작 방법:
    1 → 공부중 촬영 시작
    2 → 딴짓 촬영 시작
    3 → 졸음 촬영 시작
    s → 촬영 시작/정지 토글
    q → 종료

실행 방법:
    pip install opencv-python
    python collect_data.py
"""

import cv2
import os
import time
import numpy as np
from pathlib import Path
from datetime import datetime

# ──────────────────────────────────────────
# 설정
# ──────────────────────────────────────────

CAPTURE_FPS = 2                          # 1초에 2장 촬영
CAPTURE_INTERVAL = 1.0 / CAPTURE_FPS    # 0.5초 간격

# collect_data.py 위치 기준 경로 자동 설정
# C:\Users\정재훈\Desktop\position\StudySync\ai-server\collect_data.py
# → dataset 저장 위치: C:\Users\정재훈\Desktop\position\StudySync\dataset\
THIS_FILE     = Path(__file__).resolve()   # 현재 파일 절대경로
AI_SERVER_DIR = THIS_FILE.parent           # ai-server 폴더
STUDYSYNC_DIR = AI_SERVER_DIR.parent       # StudySync 폴더
BASE_DIR      = STUDYSYNC_DIR / "dataset"

# 클래스 정의
CLASSES = {
    "1": "focused",     # 공부중
    "2": "distracted",  # 딴짓
    "3": "drowsy",      # 졸음
}

CLASS_KR = {
    "focused":    "공부중",
    "distracted": "딴짓",
    "drowsy":     "졸음",
}

CLASS_COLOR = {
    "focused":    (0, 200, 100),   # 초록
    "distracted": (0, 140, 255),   # 주황
    "drowsy":     (60, 60, 220),   # 빨강
}


# ──────────────────────────────────────────
# 폴더 초기화
# ──────────────────────────────────────────

def init_folders() -> dict:
    """
    dataset 폴더 및 클래스별 하위 폴더 생성
    반환: 각 클래스별 폴더 경로
    """
    folders = {}
    for cls in CLASSES.values():
        folder = BASE_DIR / cls
        folder.mkdir(parents=True, exist_ok=True)
        folders[cls] = folder

    print(f"📂 저장 경로: {BASE_DIR}")
    for cls, folder in folders.items():
        count = len(list(folder.glob("*.jpg")))
        print(f"   └─ {CLASS_KR[cls]:5s} ({cls}): 기존 {count}장")
    print()
    return folders


def count_images(folders: dict) -> dict:
    """각 클래스별 현재 저장된 이미지 수 반환"""
    return {cls: len(list(folder.glob("*.jpg")))
            for cls, folder in folders.items()}


# ──────────────────────────────────────────
# UI 렌더링
# ──────────────────────────────────────────

def draw_ui(frame, current_class: str | None, is_capturing: bool, counts: dict):
    """웹캠 화면에 상태 정보 오버레이"""
    h, w = frame.shape[:2]

    # 상단 상태 박스
    cv2.rectangle(frame, (0, 0), (w, 90), (20, 20, 20), -1)

    if current_class is None:
        cv2.putText(frame, "클래스를 선택하세요: [1] 공부중  [2] 딴짓  [3] 졸음",
                    (15, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (200, 200, 200), 1)
    else:
        color = CLASS_COLOR[current_class]
        label = CLASS_KR[current_class]

        # 촬영 상태 표시
        if is_capturing:
            # 깜빡이는 빨간 점
            if int(time.time() * 2) % 2 == 0:
                cv2.circle(frame, (20, 25), 8, (0, 0, 255), -1)
            cv2.putText(frame, f"REC  [{label}] 촬영 중... [s] 정지",
                        (38, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
        else:
            cv2.putText(frame, f"[{label}] 선택됨  [s] 촬영 시작",
                        (15, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

    # 단축키 안내
    cv2.putText(frame, "[1] 공부중  [2] 딴짓  [3] 졸음  [s] 시작/정지  [q] 종료",
                (15, 62), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (150, 150, 150), 1)

    # 수집 현황 (하단)
    bar_y = h - 60
    cv2.rectangle(frame, (0, bar_y), (w, h), (20, 20, 20), -1)

    x_offset = 15
    for cls in ["focused", "distracted", "drowsy"]:
        count = counts.get(cls, 0)
        color = CLASS_COLOR[cls]
        label = CLASS_KR[cls]
        text = f"{label}: {count}장"
        cv2.putText(frame, text, (x_offset, h - 35),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

        # 진행 바 (목표 200장 기준)
        bar_w = 120
        filled = int(min(count / 200, 1.0) * bar_w)
        cv2.rectangle(frame, (x_offset, h - 22),
                      (x_offset + bar_w, h - 10), (60, 60, 60), -1)
        cv2.rectangle(frame, (x_offset, h - 22),
                      (x_offset + filled, h - 10), color, -1)
        cv2.putText(frame, f"{min(count,200)}/200",
                    (x_offset + bar_w + 5, h - 11),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.38, (150, 150, 150), 1)

        x_offset += 230

    # 촬영 중일 때 테두리 강조
    if is_capturing and current_class:
        color = CLASS_COLOR[current_class]
        cv2.rectangle(frame, (0, 0), (w - 1, h - 1), color, 3)

    return frame


# ──────────────────────────────────────────
# 메인
# ──────────────────────────────────────────

def main():
    # 폴더 초기화
    folders = init_folders()

    # 웹캠 열기
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("❌ 웹캠을 열 수 없습니다.")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    print("🎥 웹캠 시작")
    print("  [1] 공부중  [2] 딴짓  [3] 졸음 으로 클래스 선택 후")
    print("  [s] 키로 촬영 시작/정지,  [q] 키로 종료\n")

    current_class = None   # 현재 선택된 클래스
    is_capturing = False   # 촬영 중 여부
    last_capture_time = 0  # 마지막 촬영 시각

    while True:
        ret, frame = cap.read()
        if not ret:
            print("프레임 읽기 실패")
            break

        frame = cv2.flip(frame, 1)   # 거울 모드
        counts = count_images(folders)

        # 2fps 촬영 로직
        now = time.time()
        if (is_capturing
                and current_class is not None
                and now - last_capture_time >= CAPTURE_INTERVAL):

            last_capture_time = now

            # 파일명: 클래스_타임스탬프.jpg
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
            filename = f"{current_class}_{timestamp}.jpg"
            save_path = folders[current_class] / filename

            # 저장 (UI 오버레이 전 원본 프레임 저장)
            # 한글 경로 호환을 위해 imencode + tofile 사용
            ext = ".jpg"
            result, encoded = cv2.imencode(ext, frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
            if result:
                encoded.tofile(str(save_path))
            else:
                print(f"  ⚠️  인코딩 실패")

            total = counts[current_class] + 1
            print(f"  📸 저장: {filename}  ({CLASS_KR[current_class]} {total}장)")

            # 목표 달성 알림
            if total == 200:
                print(f"\n  ✅ {CLASS_KR[current_class]} 200장 수집 완료!\n")

        # UI 오버레이 후 화면 출력
        display = draw_ui(frame.copy(), current_class, is_capturing, counts)
        cv2.imshow("StudySync 데이터 수집", display)

        # 키 입력 처리
        key = cv2.waitKey(1) & 0xFF

        if key == ord("q"):
            break

        elif key == ord("1"):
            current_class = "focused"
            is_capturing = False
            print(f"▶ 클래스 선택: {CLASS_KR['focused']}")

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
                print(f"{'▶' if is_capturing else '⏸'} 촬영 {state}: {CLASS_KR[current_class]}")

    # 종료 시 최종 현황 출력
    print("\n📊 최종 수집 현황:")
    final_counts = count_images(folders)
    for cls in ["focused", "distracted", "drowsy"]:
        count = final_counts[cls]
        bar = "█" * (count // 10) + "░" * ((200 - count) // 10)
        print(f"  {CLASS_KR[cls]:5s}: {count:3d}장  [{bar}]")

    print(f"\n💾 저장 위치: {BASE_DIR}")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()