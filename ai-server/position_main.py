"""
StudySync AI 서버
-----------------
클라이언트에서 5fps로 전송하는 base64 프레임을 수신하고
MediaPipe로 분석 후 결과를 JSON으로 반환합니다.

실행 방법:
    pip install fastapi uvicorn opencv-python mediapipe numpy
    uvicorn main:app --host 0.0.0.0 --port 8001 --reload
"""

import cv2
import base64
import numpy as np
import time
from collections import defaultdict
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(title="StudySync AI Server")

# CORS 설정 (클라이언트 연결 허용)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ──────────────────────────────────────────
# 요청/응답 스키마
# ──────────────────────────────────────────

class FrameRequest(BaseModel):
    session_id: str          # 세션 식별자
    frame: str               # base64 인코딩된 이미지

class AnalysisResponse(BaseModel):
    session_id: str
    timestamp: float
    # 자세 분석
    posture_ok: bool
    neck_angle: float
    shoulder_diff: float
    # 졸음 감지
    is_drowsy: bool
    ear: float
    # 자리이탈
    is_absent: bool
    # 집중도 점수
    focus_score: int
    # 교정 메시지
    messages: list[str]


# ──────────────────────────────────────────
# 세션 상태 관리 (자리이탈 타이머용)
# ──────────────────────────────────────────

session_state = defaultdict(lambda: {
    "last_face_detected": time.time(),
    "is_absent": False,
})


# ──────────────────────────────────────────
# 유틸 함수
# ──────────────────────────────────────────

def decode_base64_frame(b64_string: str) -> np.ndarray:
    """base64 문자열 → OpenCV 이미지 변환"""
    # base64 헤더 제거 (data:image/jpeg;base64, 형태일 경우)
    if "," in b64_string:
        b64_string = b64_string.split(",")[1]

    img_bytes = base64.b64decode(b64_string)
    img_array = np.frombuffer(img_bytes, dtype=np.uint8)
    frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

    if frame is None:
        raise ValueError("이미지 디코딩 실패")

    return frame


def calculate_neck_angle(landmarks, frame_h: int, frame_w: int) -> float:
    """
    목 각도 계산 (귀 - 어깨 - 수직선 기준)
    MediaPipe Pose 랜드마크 사용
    반환값: 각도 (도), 25도 초과 시 불량
    """
    # 랜드마크 인덱스
    # 11: 왼쪽 어깨, 12: 오른쪽 어깨
    # 7: 왼쪽 귀, 8: 오른쪽 귀
    try:
        left_ear = landmarks[7]
        right_ear = landmarks[8]
        left_shoulder = landmarks[11]
        right_shoulder = landmarks[12]

        # 양쪽 평균 사용
        ear_y = (left_ear.y + right_ear.y) / 2 * frame_h
        ear_x = (left_ear.x + right_ear.x) / 2 * frame_w
        shoulder_y = (left_shoulder.y + right_shoulder.y) / 2 * frame_h
        shoulder_x = (left_shoulder.x + right_shoulder.x) / 2 * frame_w

        # 수직선과의 각도 계산
        import math
        dx = ear_x - shoulder_x
        dy = shoulder_y - ear_y  # y축 반전 (이미지 좌표)
        angle = abs(math.degrees(math.atan2(dx, dy)))
        return round(angle, 2)
    except Exception:
        return 0.0


def calculate_shoulder_diff(landmarks, frame_h: int) -> float:
    """
    어깨 기울기 계산 (좌우 어깨 y좌표 차이)
    반환값: 픽셀 차이값, 클수록 기울어짐
    """
    try:
        left_shoulder = landmarks[11]
        right_shoulder = landmarks[12]
        diff = abs(left_shoulder.y - right_shoulder.y) * frame_h
        return round(diff, 2)
    except Exception:
        return 0.0


def calculate_ear(eye_landmarks: list) -> float:
    """
    EAR (Eye Aspect Ratio) 계산
    EAR = (수직거리1 + 수직거리2) / (2 × 수평거리)
    0.25 미만이면 졸음
    """
    import math
    def dist(p1, p2):
        return math.sqrt((p1[0]-p2[0])**2 + (p1[1]-p2[1])**2)

    # eye_landmarks: [p1, p2, p3, p4, p5, p6] 순서
    # p1-p4: 수평, p2-p6, p3-p5: 수직
    try:
        vertical1 = dist(eye_landmarks[1], eye_landmarks[5])
        vertical2 = dist(eye_landmarks[2], eye_landmarks[4])
        horizontal = dist(eye_landmarks[0], eye_landmarks[3])
        ear = (vertical1 + vertical2) / (2.0 * horizontal)
        return round(ear, 4)
    except Exception:
        return 0.3  # 기본값 (정상)


def generate_messages(
    posture_ok: bool,
    neck_angle: float,
    shoulder_diff: float,
    is_drowsy: bool,
    is_absent: bool,
) -> list[str]:
    """상태별 교정 메시지 생성"""
    messages = []

    if is_absent:
        messages.append("자리를 비운 것으로 감지됐어요.")
        return messages  # 자리이탈이면 다른 메시지 불필요

    if neck_angle > 25:
        messages.append(
            f"목이 {neck_angle:.0f}도 숙여져 있어요. "
            "턱을 당기고 귀가 어깨 위에 오도록 자세를 바로 해주세요."
        )

    if shoulder_diff > 20:
        messages.append(
            "어깨가 한쪽으로 기울어져 있어요. "
            "양쪽 어깨 높이를 맞춰주세요."
        )

    if is_drowsy:
        messages.append(
            "졸음이 감지됐어요. "
            "잠깐 스트레칭을 하거나 환기를 해보세요."
        )

    if not messages:
        messages.append("자세가 올바릅니다. 집중 잘 하고 있어요!")

    return messages


def calculate_focus_score(
    posture_ok: bool,
    is_drowsy: bool,
    is_absent: bool,
    neck_angle: float,
    ear: float,
) -> int:
    """
    집중도 점수 계산 (0~100점)
    자세 30점 + 눈 25점 + 자리 25점 + 정면 20점
    """
    score = 100

    # 자리이탈 (-25점)
    if is_absent:
        return 0

    # 자세 불량 (-30점)
    if not posture_ok:
        score -= 30

    # 졸음 감지 (-25점)
    if is_drowsy:
        score -= 25

    # 목 각도에 따른 추가 감점 (최대 -20점)
    if neck_angle > 25:
        extra = min((neck_angle - 25) / 25 * 20, 20)
        score -= int(extra)

    return max(0, score)


# ──────────────────────────────────────────
# MediaPipe 초기화 (서버 시작 시 1회)
# ──────────────────────────────────────────

import mediapipe as mp

mp_pose = mp.solutions.pose
mp_face_mesh = mp.solutions.face_mesh

pose_detector = mp_pose.Pose(
    static_image_mode=True,       # 단일 프레임 처리
    model_complexity=1,
    min_detection_confidence=0.5,
)

face_mesh_detector = mp_face_mesh.FaceMesh(
    static_image_mode=True,
    max_num_faces=1,
    min_detection_confidence=0.5,
)

# 눈 랜드마크 인덱스 (EAR 계산용)
LEFT_EYE_IDX  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE_IDX = [362, 385, 387, 263, 373, 380]


# ──────────────────────────────────────────
# API 엔드포인트
# ──────────────────────────────────────────

@app.get("/")
async def health_check():
    return {"status": "ok", "server": "StudySync AI Server"}


@app.post("/analyze/frame", response_model=AnalysisResponse)
async def analyze_frame(request: FrameRequest):
    """
    클라이언트에서 5fps로 전송하는 프레임을 수신하고 분석 결과 반환
    """
    # 1. base64 → OpenCV 이미지 디코딩
    try:
        frame = decode_base64_frame(request.frame)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"프레임 디코딩 실패: {e}")

    frame_h, frame_w = frame.shape[:2]
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    # 2. MediaPipe Pose 분석 (자세)
    neck_angle = 0.0
    shoulder_diff = 0.0
    posture_ok = True

    pose_result = pose_detector.process(rgb_frame)
    if pose_result.pose_landmarks:
        lm = pose_result.pose_landmarks.landmark
        neck_angle = calculate_neck_angle(lm, frame_h, frame_w)
        shoulder_diff = calculate_shoulder_diff(lm, frame_h)
        posture_ok = (neck_angle <= 25) and (shoulder_diff <= 20)

    # 3. MediaPipe Face Mesh 분석 (졸음 + 자리이탈)
    ear = 0.3
    is_drowsy = False
    face_detected = False

    face_result = face_mesh_detector.process(rgb_frame)
    if face_result.multi_face_landmarks:
        face_detected = True
        face_lm = face_result.multi_face_landmarks[0].landmark

        # EAR 계산
        def lm_to_point(idx):
            p = face_lm[idx]
            return (p.x * frame_w, p.y * frame_h)

        left_eye_pts  = [lm_to_point(i) for i in LEFT_EYE_IDX]
        right_eye_pts = [lm_to_point(i) for i in RIGHT_EYE_IDX]

        left_ear  = calculate_ear(left_eye_pts)
        right_ear = calculate_ear(right_eye_pts)
        ear = round((left_ear + right_ear) / 2, 4)
        is_drowsy = ear < 0.25

    # 4. 자리이탈 감지 (얼굴 미탐지 3초 이상)
    state = session_state[request.session_id]
    if face_detected:
        state["last_face_detected"] = time.time()
        state["is_absent"] = False
    else:
        elapsed = time.time() - state["last_face_detected"]
        state["is_absent"] = elapsed >= 3.0

    is_absent = state["is_absent"]

    # 5. 집중도 점수 계산
    focus_score = calculate_focus_score(
        posture_ok, is_drowsy, is_absent, neck_angle, ear
    )

    # 6. 교정 메시지 생성
    messages = generate_messages(
        posture_ok, neck_angle, shoulder_diff, is_drowsy, is_absent
    )

    return AnalysisResponse(
        session_id=request.session_id,
        timestamp=time.time(),
        posture_ok=posture_ok,
        neck_angle=neck_angle,
        shoulder_diff=shoulder_diff,
        is_drowsy=is_drowsy,
        ear=ear,
        is_absent=is_absent,
        focus_score=focus_score,
        messages=messages,
    )


@app.post("/baseline/capture")
async def capture_baseline(request: FrameRequest):
    """세션 시작 시 기준 자세 좌표 저장"""
    try:
        frame = decode_base64_frame(request.frame)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"프레임 디코딩 실패: {e}")

    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    frame_h, frame_w = frame.shape[:2]

    pose_result = pose_detector.process(rgb_frame)
    if not pose_result.pose_landmarks:
        raise HTTPException(status_code=422, detail="얼굴/자세를 감지할 수 없어요. 카메라 앞에 바르게 앉아주세요.")

    lm = pose_result.pose_landmarks.landmark
    baseline = {
        "neck_angle": calculate_neck_angle(lm, frame_h, frame_w),
        "shoulder_diff": calculate_shoulder_diff(lm, frame_h),
    }

    # 세션별 기준 저장
    session_state[request.session_id]["baseline"] = baseline

    return {
        "status": "ok",
        "session_id": request.session_id,
        "baseline": baseline,
        "message": "기준 자세가 저장됐어요.",
    }


@app.post("/traindata")
async def save_train_data(request: dict):
    """Phase 2 재학습용 라벨 데이터 저장"""
    # 추후 DB 연동 시 확장
    return {"status": "ok", "message": "학습 데이터 저장 완료"}


@app.delete("/session/{session_id}")
async def end_session(session_id: str):
    """세션 종료 시 상태 초기화"""
    if session_id in session_state:
        del session_state[session_id]
    return {"status": "ok", "session_id": session_id}