"""
test_posture.py — MediaPipe 자세 분석 시각화 테스트 클라이언트
=============================================================
실행 전 AI 서버를 먼저 켜두세요:
  python -m uvicorn position_main:app --host 0.0.0.0 --port 8001

실행 방법:
  pip install requests opencv-python
  python test_posture.py

조작 키:
  b → 기준 자세 캡처 (세션 시작 시 바른 자세로 먼저 누르세요)
  r → 세션 리셋
  q → 종료
"""

import cv2
import base64
import requests
import time
import uuid
import os
from datetime import datetime

# ─────────────────────────────────────────────
# 설정
# ─────────────────────────────────────────────
AI_SERVER_URL  = "http://localhost:8001"
TARGET_FPS     = 30                        # 서버 전송 FPS
INTERVAL       = 1.0 / TARGET_FPS

# 이벤트 저장 설정
SAVE_DIR           = r"C:\Users\lms\Desktop\Data"   # 저장 경로
BAD_SCORE_THRESHOLD = 60                            # 이 점수 미만이면 이벤트로 저장
SAVE_COOLDOWN      = 1.0                            # 연속 저장 방지: 최소 1초 간격

SESSION_ID = str(uuid.uuid4())[:8]     # 세션 고유 ID (매 실행마다 새로 생성)

# ─────────────────────────────────────────────
# 색상 상수 (BGR)
# ─────────────────────────────────────────────
GREEN  = (50,  200, 50)
YELLOW = (0,   200, 220)
RED    = (50,  50,  220)
WHITE  = (255, 255, 255)
BLACK  = (0,   0,   0)
GRAY   = (150, 150, 150)
PURPLE = (180, 80,  180)


# ─────────────────────────────────────────────
# 유틸 함수
# ─────────────────────────────────────────────

def encode_frame(frame) -> str:
    """OpenCV 프레임 → base64 문자열"""
    _, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
    return base64.b64encode(buf).decode("utf-8")


def save_event_frame(frame, result: dict, saved_count: int) -> str:
    """
    자세 이벤트(점수 60 미만) 발생 시 프레임을 파일로 저장한다.
    파일명: event_0001_score45_BAD_POSTURE_20250506_153012.jpg
    반환값: 저장된 파일 경로
    """
    os.makedirs(SAVE_DIR, exist_ok=True)

    score = result.get("focus_score", 0)
    state = ("ABSENT"      if result.get("is_absent")  else
             "DROWSY"      if result.get("is_drowsy")  else
             "BAD_POSTURE" if not result.get("posture_ok") else "LOW_SCORE")
    ts    = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f"event_{saved_count:04d}_score{score}_{state}_{ts}.jpg"
    fpath = os.path.join(SAVE_DIR, fname)

    # 저장 전 프레임에 점수/상태 텍스트를 태워서 저장
    annotated = frame.copy()
    cv2.putText(annotated, f"Score:{score} | {state}",
                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 220), 2)
    cv2.putText(annotated, ts,
                (10, 58), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
    cv2.imwrite(fpath, annotated)
    return fpath


def capture_baseline(frame) -> dict | None:
    """현재 프레임을 기준 자세로 서버에 등록한다."""
    try:
        resp = requests.post(
            f"{AI_SERVER_URL}/baseline/capture",
            json={"session_id": SESSION_ID, "frame": encode_frame(frame)},
            timeout=3.0,
        )
        return resp.json()
    except Exception as e:
        return {"status": "error", "message": str(e)}


def analyze_frame(frame) -> dict | None:
    """프레임을 서버로 보내 분석 결과를 받는다."""
    try:
        resp = requests.post(
            f"{AI_SERVER_URL}/analyze/frame",
            json={"session_id": SESSION_ID, "frame": encode_frame(frame)},
            timeout=2.0,
        )
        return resp.json()
    except Exception:
        return None


# ─────────────────────────────────────────────
# 화면 렌더링
# ─────────────────────────────────────────────

def draw_score_bar(img, x, y, w, h, score: int, label: str):
    """집중도 점수를 Progress Bar로 그린다."""
    # 배경 바
    cv2.rectangle(img, (x, y), (x + w, y + h), (60, 60, 60), -1)

    # 점수에 따른 색상
    if score >= 70:
        color = GREEN
    elif score >= 40:
        color = YELLOW
    else:
        color = RED

    filled = int(w * score / 100)
    cv2.rectangle(img, (x, y), (x + filled, y + h), color, -1)

    # 테두리
    cv2.rectangle(img, (x, y), (x + w, y + h), WHITE, 1)

    # 라벨 + 수치
    cv2.putText(img, f"{label}: {score}",
                (x, y - 6), cv2.FONT_HERSHEY_SIMPLEX, 0.45, WHITE, 1)


def draw_metric_row(img, x, y, label: str, value: str, ok: bool):
    """단일 지표를 한 줄로 그린다. 정상이면 초록, 아니면 빨강."""
    color = GREEN if ok else RED
    icon  = "O" if ok else "X"
    cv2.putText(img, f"[{icon}] {label}: {value}",
                (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.48, color, 1)


def draw_overlay(img, result: dict, baseline_set: bool, server_ok: bool):
    """
    왼쪽 상단 패널에 분석 결과를 표시한다.
    """
    h, w = img.shape[:2]

    # ── 반투명 패널 (왼쪽) ──
    panel_w = 310
    overlay = img.copy()
    cv2.rectangle(overlay, (0, 0), (panel_w, h), (20, 20, 20), -1)
    cv2.addWeighted(overlay, 0.55, img, 0.45, 0, img)

    # ── 타이틀 ──
    cv2.putText(img, "StudySync Posture Test",
                (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6, WHITE, 1)
    cv2.putText(img, f"Session: {SESSION_ID}",
                (10, 44), cv2.FONT_HERSHEY_SIMPLEX, 0.38, GRAY, 1)

    # ── 서버 연결 상태 ──
    sv_color = GREEN if server_ok else RED
    sv_text  = "Server: Connected" if server_ok else "Server: OFFLINE"
    cv2.putText(img, sv_text, (10, 62), cv2.FONT_HERSHEY_SIMPLEX, 0.42, sv_color, 1)

    # ── 기준 자세 상태 ──
    bl_color = GREEN if baseline_set else YELLOW
    bl_text  = "Baseline: SET  (b=reset)" if baseline_set else "Baseline: NOT SET  -> press [b]"
    cv2.putText(img, bl_text, (10, 78), cv2.FONT_HERSHEY_SIMPLEX, 0.42, bl_color, 1)

    if result is None:
        cv2.putText(img, "No data (waiting...)",
                    (10, 110), cv2.FONT_HERSHEY_SIMPLEX, 0.5, GRAY, 1)
        return

    # ── 집중도 점수 바 ──
    score = result.get("focus_score", 0)
    draw_score_bar(img, 10, 104, panel_w - 20, 16, score, "Focus Score")

    # ── 상태 뱃지 ──
    state = result.get("messages", [""])[0] if result.get("messages") else "-"
    if result.get("is_absent"):
        badge, bc = "ABSENT", RED
    elif result.get("is_drowsy"):
        badge, bc = "DROWSY", PURPLE
    elif result.get("posture_ok"):
        badge, bc = "GOOD", GREEN
    else:
        badge, bc = "BAD POSTURE", YELLOW

    badge_x, badge_y = 10, 136
    (tw, th), _ = cv2.getTextSize(badge, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 2)
    cv2.rectangle(img, (badge_x - 4, badge_y - th - 4),
                       (badge_x + tw + 4, badge_y + 4), bc, -1)
    cv2.putText(img, badge, (badge_x, badge_y),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, BLACK, 2)

    # ── 세부 지표 ──
    neck  = result.get("neck_angle", 0.0)
    ear   = result.get("ear", 0.0)
    sdiff = result.get("shoulder_diff", 0.0)

    cy = 168
    draw_metric_row(img, 10, cy,      "Neck Angle ", f"{neck:.1f} deg", neck <= 25)
    draw_metric_row(img, 10, cy + 20, "EAR        ", f"{ear:.3f}",     ear >= 0.25)
    draw_metric_row(img, 10, cy + 40, "Shoulder   ", f"{sdiff:.1f} px", sdiff <= 20)
    draw_metric_row(img, 10, cy + 60, "Posture OK ", str(result.get("posture_ok")),
                    result.get("posture_ok", False))
    draw_metric_row(img, 10, cy + 80, "Drowsy     ", str(result.get("is_drowsy")),
                    not result.get("is_drowsy", False))
    draw_metric_row(img, 10, cy + 100,"Absent     ", str(result.get("is_absent")),
                    not result.get("is_absent", False))

    # ── 교정 메시지 ──
    msgs = result.get("messages", [])
    if msgs:
        cv2.putText(img, "Guide:", (10, cy + 130),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, GRAY, 1)
        # 한 줄에 최대 38자씩 줄바꿈
        msg = msgs[0]
        lines = [msg[i:i+38] for i in range(0, min(len(msg), 76), 38)]
        for i, line in enumerate(lines):
            cv2.putText(img, line, (10, cy + 148 + i * 18),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.38, WHITE, 1)

    # ── 하단 키 안내 ──
    cv2.putText(img, "[b] Baseline  [r] Reset  [q] Quit",
                (10, h - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.38, GRAY, 1)


# ─────────────────────────────────────────────
# 서버 연결 확인
# ─────────────────────────────────────────────

def check_server() -> bool:
    try:
        r = requests.get(f"{AI_SERVER_URL}/", timeout=2.0)
        return r.status_code == 200
    except Exception:
        return False


# ─────────────────────────────────────────────
# 메인 루프
# ─────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  StudySync 자세 분석 테스트 클라이언트")
    print("=" * 55)
    print(f"  AI 서버: {AI_SERVER_URL}")
    print(f"  세션 ID: {SESSION_ID}")
    print()

    # 서버 연결 확인
    server_ok = check_server()
    if server_ok:
        print("  [OK] 서버 연결 성공!")
    else:
        print("  [!!] 서버에 연결할 수 없습니다.")
        print("       position_main.py 를 먼저 실행하세요.")
        print("       테스트는 계속하지만 결과가 표시되지 않습니다.")
    print()
    print("  [b] 바른 자세 유지 후 눌러서 기준 자세 등록")
    print("  [q] 종료")
    print()

    # 웹캠 열기
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("  [ERR] 웹캠을 열 수 없습니다.")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    last_send_time  = 0.0
    last_result     = None
    baseline_set    = False
    frame_count     = 0
    saved_count     = 0          # 저장된 이벤트 수
    last_save_time  = 0.0        # 마지막 저장 시각 (쿨다운용)

    print(f"  이벤트 저장 경로: {SAVE_DIR}")
    print(f"  이벤트 기준: focus_score < {BAD_SCORE_THRESHOLD}")
    print()

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        frame = cv2.flip(frame, 1)   # 거울 모드 (자연스러운 자기 확인)
        now   = time.time()

        # ── 30fps 로 서버에 전송 ──
        if server_ok and (now - last_send_time) >= INTERVAL:
            last_send_time = now
            frame_count   += 1

            result = analyze_frame(frame)
            if result:
                last_result = result
                score = result.get("focus_score", 0)
                neck  = result.get("neck_angle", 0.0)
                ear   = result.get("ear", 0.0)
                state = ("ABSENT" if result.get("is_absent") else
                         "DROWSY" if result.get("is_drowsy") else
                         "OK"     if result.get("posture_ok") else "BAD")

                # ── 콘솔 로그 (30프레임마다 = 약 1초) ──
                if frame_count % 30 == 0:
                    print(f"  Frame {frame_count:5d} | "
                          f"Score={score:3d} | "
                          f"Neck={neck:5.1f}° | "
                          f"EAR={ear:.3f} | "
                          f"State={state} | "
                          f"Saved={saved_count}")

                # ── 이벤트 감지 → 프레임 저장 ──
                is_bad_event = score < BAD_SCORE_THRESHOLD
                cooldown_ok  = (now - last_save_time) >= SAVE_COOLDOWN

                if is_bad_event and cooldown_ok:
                    saved_count   += 1
                    last_save_time = now
                    saved_path     = save_event_frame(frame, result, saved_count)
                    print(f"  [SAVE #{saved_count:04d}] Score={score} | {state} → {saved_path}")

            else:
                # 연결 재확인
                server_ok = check_server()

        # ── 오버레이 그리기 ──
        display = frame.copy()
        draw_overlay(display, last_result, baseline_set, server_ok)

        cv2.imshow("StudySync Posture Test", display)

        # ── 키 입력 ──
        key = cv2.waitKey(1) & 0xFF

        if key == ord("q"):
            print("\n  [종료]")
            break

        elif key == ord("b"):
            # 기준 자세 캡처
            print("\n  [기준 자세 캡처 중...]")
            resp = capture_baseline(frame)
            if resp and resp.get("status") == "ok":
                baseline_set = True
                baseline = resp.get("baseline", {})
                print(f"  [OK] 기준 자세 등록 완료!")
                print(f"       목 각도: {baseline.get('neck_angle', 'N/A'):.1f}°")
                print(f"       어깨 기울기: {baseline.get('shoulder_diff', 'N/A'):.1f}px")
            else:
                msg = resp.get("detail") or resp.get("message", "실패") if resp else "서버 응답 없음"
                print(f"  [!!] 기준 자세 등록 실패: {msg}")
                print("       카메라 앞에 바르게 앉은 뒤 다시 시도하세요.")
            print()

        elif key == ord("r"):
            # 세션 리셋 (서버 세션 상태 초기화)
            try:
                requests.delete(f"{AI_SERVER_URL}/session/{SESSION_ID}", timeout=2.0)
            except Exception:
                pass
            last_result    = None
            baseline_set   = False
            frame_count    = 0
            saved_count    = 0
            last_save_time = 0.0
            print("\n  [세션 리셋]")

    # 정리
    try:
        requests.delete(f"{AI_SERVER_URL}/session/{SESSION_ID}", timeout=1.0)
    except Exception:
        pass

    cap.release()
    cv2.destroyAllWindows()

    print()
    print("=" * 55)
    print(f"  총 분석 프레임 : {frame_count}")
    print(f"  저장된 이벤트  : {saved_count} 개")
    if saved_count > 0:
        print(f"  저장 경로      : {SAVE_DIR}")
    print("=" * 55)


if __name__ == "__main__":
    main()