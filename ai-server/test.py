"""
테스트 클라이언트
-----------------
웹캠에서 5fps로 프레임을 캡처해서 AI 서버로 전송하는 테스트 스크립트
실제 PyQt 클라이언트 구현 전 AI 서버 동작 확인용

실행 방법:
    pip install requests opencv-python
    python test_client.py
"""

import cv2
import base64
import requests
import time
import json

AI_SERVER_URL = "http://localhost:8001"
SESSION_ID = "test_session_001"
TARGET_FPS = 5
FRAME_INTERVAL = 1.0 / TARGET_FPS  # 0.2초


def encode_frame_to_base64(frame) -> str:
    """OpenCV 프레임 → base64 문자열 변환"""
    _, buffer = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
    return base64.b64encode(buffer).decode("utf-8")


def send_frame(frame) -> dict:
    """AI 서버로 프레임 전송 후 결과 반환"""
    b64_frame = encode_frame_to_base64(frame)
    response = requests.post(
        f"{AI_SERVER_URL}/analyze/frame",
        json={"session_id": SESSION_ID, "frame": b64_frame},
        timeout=2.0,
    )
    return response.json()


def main():
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("웹캠을 열 수 없습니다.")
        return

    print(f"AI 서버 연결: {AI_SERVER_URL}")
    print(f"전송 속도: {TARGET_FPS}fps")
    print("'q' 키로 종료\n")

    frame_count = 0
    last_send_time = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        frame = cv2.flip(frame, 1)
        now = time.time()

        # 5fps 제어
        if now - last_send_time >= FRAME_INTERVAL:
            last_send_time = now
            frame_count += 1

            try:
                result = send_frame(frame)

                # 결과 출력
                print(f"[프레임 {frame_count}]")
                print(f"  집중도: {result['focus_score']}점")
                print(f"  목 각도: {result['neck_angle']}도 | 자세: {'✓' if result['posture_ok'] else '✗'}")
                print(f"  EAR: {result['ear']} | 졸음: {'감지' if result['is_drowsy'] else '없음'}")
                print(f"  자리이탈: {'감지' if result['is_absent'] else '없음'}")
                print(f"  메시지: {result['messages'][0]}")
                print()

                # 화면에 결과 오버레이
                score_text = f"Focus: {result['focus_score']}pt"
                color = (0, 200, 100) if result['posture_ok'] else (0, 0, 220)
                cv2.putText(frame, score_text, (20, 40),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.0, color, 2)
                cv2.putText(frame, result['messages'][0][:40], (20, 80),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

            except requests.exceptions.ConnectionError:
                print("AI 서버에 연결할 수 없어요. 서버가 실행 중인지 확인해주세요.")
            except Exception as e:
                print(f"오류: {e}")

        cv2.imshow("StudySync Test Client", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()