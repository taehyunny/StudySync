"""
StudySync AI 서버 — 클라이언트 TCP 통신 + 계산 로직
======================================================
predictor.py 와 main_ai.py 를 import해서 사용

흐름:
    1. 클라이언트로부터 프레임 수신 (2000)
    2. TCN 추론 (predictor.py)
    3. 계산 로직 (focus_score, posture_ok, guide 등)
    4. 클라이언트 응답 (2001) - 명세서 기준
    5. 딴짓/졸음 이벤트 발생 시 메인서버 push (main_ai.py)

수신 필드 (클라이언트 → AI 서버):
    protocol_no, session_id, frame_id, timestamp_ms,
    ear, neck_angle, shoulder_diff, head_yaw, head_pitch, face_detected

응답 필드 (AI 서버 → 클라이언트):
    protocol_no, timestamp_ms, state, focus_score,
    confidence, posture_ok, is_drowsy, is_absent, guide

실행:
    python tcp_server.py
"""

import asyncio
import json
import struct
import logging
from collections import defaultdict
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Optional

from predictor import load_model, predict        # TCN 추론
from main_ai import MainServerClient             # 메인서버 통신

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)
logger = logging.getLogger(__name__)


# ──────────────────────────────────────────
# 설정
# ──────────────────────────────────────────

HOST = "0.0.0.0"
PORT = 9500

MAIN_SERVER_HOST = "10.10.10.130"
MAIN_SERVER_PORT = 9000

SEQUENCE_LEN           = 150
STRIDE                 = 75
POSTURE_NECK_THRESHOLD = 25.0

PROTOCOL_FRAME = 2000
PROTOCOL_RES   = 2001
PROTOCOL_CALIB = 2002   # 캘리브레이션 프레임
PROTOCOL_CALIB_ACK = 2003  # 캘리브레이션 완료 응답


# ──────────────────────────────────────────
# 계산 로직
# ──────────────────────────────────────────

def calc_focus_score(state: str, confidence: float) -> int:
    """state + confidence → 집중도 점수 (0~100)"""
    base = {"focus": 100, "distracted": 30, "drowsy": 20, "absent": 0}
    score = base.get(state, 50)
    return max(0, min(100, int(score * (0.7 + 0.3 * confidence))))


def calc_posture_ok(neck_angle: float) -> bool:
    """목 각도 기반 자세 정상 여부"""
    return neck_angle <= POSTURE_NECK_THRESHOLD


def generate_guide(state: str, posture_ok: bool) -> str:
    """상태 기반 사용자 안내 메시지"""
    if state == "absent":
        return "자리를 비운 것이 감지됐어요."
    if state == "drowsy":
        return "졸음이 감지됐어요. 잠깐 스트레칭 해보세요!"
    if state == "distracted":
        return "집중해주세요!"
    if not posture_ok:
        return "자세를 바르게 해주세요."
    return "공부 중이에요. 집중하고 있어요!"


def run_calculation(packet: dict, state: str, confidence: float) -> dict:
    """
    TCN 추론 결과 + 클라이언트 원본 데이터 → 최종 계산 결과
    반환값이 클라이언트 응답 필드가 됨
    """
    neck_angle    = float(packet.get("neck_angle", 0.0))
    face_detected = int(packet.get("face_detected", 1))

    is_absent   = face_detected == 0
    is_drowsy   = state == "drowsy"
    posture_ok  = calc_posture_ok(neck_angle)
    final_state = "absent" if is_absent else state
    focus_score = calc_focus_score(final_state, confidence)
    guide       = generate_guide(final_state, posture_ok)

    return {
        "state":       final_state,
        "focus_score": focus_score,
        "confidence":  confidence,
        "posture_ok":  posture_ok,
        "is_drowsy":   is_drowsy,
        "is_absent":   is_absent,
        "guide":       guide,
    }


# ──────────────────────────────────────────
# 패킷 유틸
# ──────────────────────────────────────────

async def read_packet(reader: asyncio.StreamReader) -> Optional[dict]:
    """[4byte 길이][JSON] 패킷 수신"""
    try:
        hdr    = await reader.readexactly(4)
        length = struct.unpack(">I", hdr)[0]

        if length == 0 or length > 65536:
            logger.error(f"비정상 패킷 길이: {length}")
            return None

        body = await reader.readexactly(length)
        return json.loads(body.decode("utf-8"))

    except asyncio.IncompleteReadError:
        return None
    except json.JSONDecodeError as e:
        logger.error(f"JSON 파싱 오류: {e}")
        return None
    except Exception as e:
        logger.error(f"패킷 수신 오류: {type(e).__name__}: {e}")
        return None


async def send_packet(writer: asyncio.StreamWriter, payload: dict):
    """[4byte 길이][JSON] 패킷 송신"""
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    writer.write(struct.pack(">I", len(body)) + body)
    await writer.drain()


def now_ms() -> int:
    return int(datetime.now().timestamp() * 1000)


# ──────────────────────────────────────────
# 클라이언트 핸들러
# ──────────────────────────────────────────

async def handle_client(reader, writer, model, scaler_mean, scaler_std,
                         label_names, main_client: MainServerClient):
    """연결된 클라이언트 1개 처리"""
    addr = writer.get_extra_info("peername")
    logger.info(f"🔗 클라이언트 연결: {addr}")

    session_buffers:    dict[str, list]  = defaultdict(list)
    session_last_state: dict[str, str]   = {}
    session_last_conf:  dict[str, float] = {}
    # 캘리브레이션 관련
    calib_buffers:  dict[str, list]  = defaultdict(list)   # 캘리브레이션 프레임 버퍼
    calib_baseline: dict[str, dict]  = {}                  # 세션별 기준값

    try:
        while True:
            packet = await read_packet(reader)

            if packet is None:
                break

            proto = packet.get("protocol_no")

            # ── 캘리브레이션 프레임 처리 (2002)
            if proto == PROTOCOL_CALIB:
                session_id = packet.get("session_id", 0)

                # 캘리브레이션 버퍼에 특징값 쌓기
                calib_buffers[session_id].append({
                    "ear":           float(packet.get("ear",           0.0)),
                    "neck_angle":    float(packet.get("neck_angle",    0.0)),
                    "shoulder_diff": float(packet.get("shoulder_diff", 0.0)),
                    "head_yaw":      float(packet.get("head_yaw",      0.0)),
                    "head_pitch":    float(packet.get("head_pitch",    0.0)),
                })

                logger.info(
                    f"[CALIB] [{session_id}] "
                    f"{len(calib_buffers[session_id])}프레임 수집 중... "
                    f"ear={packet.get('ear',0):.3f} "
                    f"neck={packet.get('neck_angle',0):.2f}"
                )

                # 캘리브레이션 ACK 응답
                await send_packet(writer, {
                    "protocol_no": PROTOCOL_CALIB_ACK,
                    "session_id":  session_id,
                    "timestamp_ms": now_ms(),
                    "calib_frames": len(calib_buffers[session_id]),
                })
                continue

            # ── 일반 프레임이 아니면 무시
            if proto != PROTOCOL_FRAME:
                logger.warning(f"알 수 없는 protocol_no: {proto}")
                continue

            session_id    = packet.get("session_id", 0)
            timestamp_ms  = packet.get("timestamp_ms", now_ms())
            face_detected = int(packet.get("face_detected", 1))

            # ── 캘리브레이션 기준값 계산 (캘리브레이션 버퍼가 있고 아직 기준값 없을 때)
            if calib_buffers[session_id] and session_id not in calib_baseline:
                buf = calib_buffers[session_id]
                calib_baseline[session_id] = {
                    "ear":           sum(f["ear"]           for f in buf) / len(buf),
                    "neck_angle":    sum(f["neck_angle"]    for f in buf) / len(buf),
                    "shoulder_diff": sum(f["shoulder_diff"] for f in buf) / len(buf),
                    "head_yaw":      sum(f["head_yaw"]      for f in buf) / len(buf),
                    "head_pitch":    sum(f["head_pitch"]    for f in buf) / len(buf),
                }
                logger.info(
                    f"[CALIB] [{session_id}] 기준값 확정 "
                    f"({len(buf)}프레임 평균) | "
                    f"ear={calib_baseline[session_id]['ear']:.3f} | "
                    f"neck={calib_baseline[session_id]['neck_angle']:.2f} | "
                    f"shdiff={calib_baseline[session_id]['shoulder_diff']:.2f}"
                )

            # 수신 데이터 확인용 로그 (임시)
            logger.info(
                f"[RAW] ear={packet.get('ear',0):.3f} "
                f"neck={packet.get('neck_angle',0)} "
                f"shdiff={packet.get('shoulder_diff',0)} "
                f"yaw={packet.get('head_yaw',0):.2f} "
                f"pitch={packet.get('head_pitch',0):.2f} "
                f"face={packet.get('face_detected',1)}"
            )

            # ── 얼굴 미감지 → absent 처리
            if face_detected == 0:
                session_buffers[session_id].clear()
                session_last_state[session_id] = "absent"
                session_last_conf[session_id]  = 0.0

                result = run_calculation(packet, "absent", 0.0)
                await send_packet(writer, {
                    "protocol_no": PROTOCOL_RES,
                    "timestamp_ms": now_ms(),
                    **result,
                })
                # absent는 이벤트 아님 → 메인서버 push 안 함
                continue

            # ── 특징값 추출 (7차원, phone_detected=0 고정)
            # EAR 비율 변환은 predictor.py의 predict()에서 수행
            features = [
                float(packet.get("ear",           0.0)),
                float(packet.get("neck_angle",    0.0)),
                float(packet.get("shoulder_diff", 0.0)),
                float(packet.get("head_yaw",      0.0)),
                float(packet.get("head_pitch",    0.0)),
                float(face_detected),
                0.0,   # phone_detected 고정
            ]
            session_buffers[session_id].append(features)
            buffered_now = len(session_buffers[session_id])

            # 30프레임마다 버퍼 상태 출력
            if buffered_now % 30 == 0:
                logger.info(
                    f"[BUFFER] [{session_id}] "
                    f"{buffered_now}/{SEQUENCE_LEN}프레임 누적 중..."
                )

            # ── 150프레임 미달 → 마지막 추론 결과 유지
            if len(session_buffers[session_id]) < SEQUENCE_LEN:
                last_state = session_last_state.get(session_id, "focus")
                last_conf  = session_last_conf.get(session_id, 0.0)
                result = run_calculation(packet, last_state, last_conf)
                await send_packet(writer, {
                    "protocol_no": PROTOCOL_RES,
                    "timestamp_ms": now_ms(),
                    **result,
                })
                continue

            # ── 150프레임 달성 → TCN 추론 (predictor.py)
            # 캘리브레이션 기준 EAR 있으면 비율 변환 후 추론
            baseline_ear = None
            if session_id in calib_baseline:
                baseline_ear = calib_baseline[session_id]["ear"]

            state, confidence = predict(
                model, scaler_mean, scaler_std, label_names,
                session_buffers[session_id][-SEQUENCE_LEN:],
                baseline_ear=baseline_ear,
            )
            session_last_state[session_id] = state
            session_last_conf[session_id]  = confidence
            session_buffers[session_id].clear()   # 학습 방식과 동일하게 완전 초기화

            # ── 계산 로직 (TCN 결과 + 원본 데이터)
            result = run_calculation(packet, state, confidence)

            # ── 클라이언트 응답 (매 추론마다 항상 전송)
            await send_packet(writer, {
                "protocol_no": PROTOCOL_RES,
                "timestamp_ms": now_ms(),
                **result,
            })

            # 클라이언트 응답 로그 (focus 포함 모든 추론 결과)
            _posture = "OK" if result["posture_ok"] else "BAD"
            _guide   = result["guide"]
            _state   = result["state"]
            _score   = result["focus_score"]
            logger.info(
                f"[CLIENT] [{session_id}] 응답완료 | "
                f"state={_state} | score={_score} | "
                f"conf={confidence:.2f} | posture={_posture} | guide={_guide}"
            )

            # ── 메인서버 push (distracted / drowsy 이벤트 발생 시에만)
            if result["state"] in ("distracted", "drowsy"):
                event_label = "졸음 감지" if result["state"] == "drowsy" else "딴짓 감지"
                logger.info(f"[EVENT] [{session_id}] {event_label}! 클라이언트 응답 완료")

                asyncio.create_task(main_client.push_focus_log(
                    session_id    = session_id,
                    focus_score   = result["focus_score"],
                    state         = result["state"],
                    is_absent     = result["is_absent"],
                    is_drowsy     = result["is_drowsy"],
                    # 재학습용 7개 특징값
                    ear           = float(packet.get("ear",           0.0)),
                    neck_angle    = float(packet.get("neck_angle",    0.0)),
                    shoulder_diff = float(packet.get("shoulder_diff", 0.0)),
                    head_yaw      = float(packet.get("head_yaw",      0.0)),
                    head_pitch    = float(packet.get("head_pitch",    0.0)),
                    face_detected = int(packet.get("face_detected",   1)),
                    phone_detected= 0,
                    timestamp_ms  = timestamp_ms,
                ))

                logger.info(f"[EVENT] [{session_id}] {event_label}! 메인서버 push 전송")

    except ConnectionResetError:
        logger.info(f"❌ 클라이언트 연결 끊김: {addr}")
    except Exception as e:
        logger.error(f"클라이언트 처리 오류: {e}")
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass
        logger.info(f"🔌 클라이언트 해제: {addr}")


# ──────────────────────────────────────────
# 서버 시작
# ──────────────────────────────────────────

async def main():
    logger.info("=" * 55)
    logger.info("📡 StudySync AI 서버 시작")
    logger.info("=" * 55)

    # 1. TCN 모델 로드 (predictor.py)
    try:
        model, scaler_mean, scaler_std, label_names = load_model()
    except FileNotFoundError as e:
        logger.error(str(e))
        logger.error("train_tcn.py 로 먼저 모델을 학습시켜주세요.")
        return

    # 2. 메인서버 연결 (main_ai.py)
    main_client = MainServerClient(MAIN_SERVER_HOST, MAIN_SERVER_PORT)
    await main_client.connect()

    # 3. 클라이언트 TCP 서버 시작
    server = await asyncio.start_server(
        lambda r, w: handle_client(
            r, w, model, scaler_mean, scaler_std, label_names, main_client
        ),
        HOST, PORT
    )

    logger.info(f"✅ 클라이언트 대기: {HOST}:{PORT}")
    logger.info(f"   메인서버: {MAIN_SERVER_HOST}:{MAIN_SERVER_PORT} "
                f"({'연결됨' if main_client._connected else '연결 대기'})")
    logger.info(f"   시퀀스: {SEQUENCE_LEN}프레임 / stride {STRIDE}프레임")
    logger.info(f"   이벤트 전송: distracted / drowsy 일 때만")
    logger.info("=" * 55)

    try:
        async with server:
            await server.serve_forever()
    finally:
        await main_client.close()


if __name__ == "__main__":
    asyncio.run(main())