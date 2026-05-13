"""
StudySync AI 서버 → 메인서버 TCP 통신 모듈
============================================
영구 연결, 자동 재연결, ACK 기반 재전송 지원

프로토콜:
    [4byte big-endian JSON 길이][JSON 페이로드]

프로토콜 번호:
    1000  AI → 메인  FOCUS_LOG_PUSH       (졸음/딴짓 이벤트 + 7개 특징값)
    1001  메인 → AI  FOCUS_LOG_ACK
    1004  AI → 메인  POSTURE_EVENT_PUSH
    1005  메인 → AI  POSTURE_EVENT_ACK
    1010  메인 → AI  MODEL_RELOAD_CMD
    1011  AI → 메인  MODEL_RELOAD_RES
    1200  메인 → AI  HEALTH_PING
    1201  AI → 메인  HEALTH_PONG

사용 예시:
    from main_ai import MainServerClient

    client = MainServerClient("10.10.10.130", 9000)
    await client.connect()

    # 졸음/딴짓 이벤트 발생 시에만 호출
    await client.push_focus_log(
        session_id=42, focus_score=20, state="drowsy",
        is_absent=False, is_drowsy=True,
        ear=0.18, neck_angle=15.2, shoulder_diff=3.1,
        head_yaw=-5.2, head_pitch=8.3, face_detected=1,
    )
"""

import asyncio
import json
import struct
import uuid
import logging
from datetime import datetime, timezone, timedelta
from typing import Optional

logger = logging.getLogger(__name__)


# ──────────────────────────────────────────
# 프로토콜 번호 상수
# ──────────────────────────────────────────

class Protocol:
    # AI → 메인
    FOCUS_LOG_PUSH     = 1000
    POSTURE_EVENT_PUSH = 1004
    MODEL_RELOAD_RES   = 1011
    HEALTH_PONG        = 1201

    # 메인 → AI
    FOCUS_LOG_ACK      = 1001
    POSTURE_EVENT_ACK  = 1005
    MODEL_RELOAD_CMD   = 1010
    HEALTH_PING        = 1200


# ──────────────────────────────────────────
# 메인서버 TCP 클라이언트
# ──────────────────────────────────────────

class MainServerClient:
    """
    AI 서버 → 메인서버 TCP 통신

    특징:
        - 영구 연결 유지
        - 끊김 시 2초 간격 자동 재연결
        - ACK 기반 재전송 (3초 타임아웃, 최대 3회)
        - HEALTH_PING 자동 PONG 응답
        - MODEL_RELOAD_CMD 콜백 처리
    """

    PROTOCOL_VERSION   = "1.0"
    RECONNECT_INTERVAL = 2.0
    ACK_TIMEOUT        = 3.0
    MAX_RETRY          = 3

    def __init__(self, host: str, port: int, on_model_reload=None):
        self.host             = host
        self.port             = port
        self.on_model_reload  = on_model_reload
        self._reader          = None
        self._writer          = None
        self._connected       = False
        self._pending_acks    = {}
        self._reader_task     = None
        self._reconnect_task  = None

    # ──────────────────────────────────────
    # 연결 관리
    # ──────────────────────────────────────

    async def connect(self):
        """초기 연결 시작 + 자동 재연결 태스크 가동"""
        self._reconnect_task = asyncio.create_task(self._reconnect_loop())
        for _ in range(10):
            if self._connected:
                return
            await asyncio.sleep(0.3)

    async def _reconnect_loop(self):
        while True:
            if not self._connected:
                try:
                    logger.info(f"메인서버 연결 시도: {self.host}:{self.port}")
                    self._reader, self._writer = await asyncio.open_connection(
                        self.host, self.port
                    )
                    self._connected = True
                    logger.info("✅ 메인서버 연결 성공")
                    self._reader_task = asyncio.create_task(self._receive_loop())
                except (ConnectionRefusedError, OSError) as e:
                    logger.warning(f"메인서버 연결 실패: {e}")
            await asyncio.sleep(self.RECONNECT_INTERVAL)

    async def _on_disconnect(self):
        if not self._connected:
            return
        logger.warning("⚠️  메인서버 연결 끊김 - 재연결 대기")
        self._connected = False
        for fut in self._pending_acks.values():
            if not fut.done():
                fut.set_exception(ConnectionError("메인서버 연결 끊김"))
        self._pending_acks.clear()
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
        self._reader = self._writer = None

    # ──────────────────────────────────────
    # 패킷 수신
    # ──────────────────────────────────────

    async def _receive_loop(self):
        try:
            while self._connected:
                length_bytes = await self._reader.readexactly(4)
                json_bytes   = await self._reader.readexactly(
                    struct.unpack(">I", length_bytes)[0]
                )
                await self._dispatch(json.loads(json_bytes.decode("utf-8")))
        except asyncio.IncompleteReadError:
            logger.warning("메인서버가 연결을 닫음")
        except Exception as e:
            logger.error(f"수신 루프 오류: {e}")
        finally:
            await self._on_disconnect()

    async def _dispatch(self, payload: dict):
        proto = payload.get("protocol_no")

        # ACK 처리
        if proto in (Protocol.FOCUS_LOG_ACK, Protocol.POSTURE_EVENT_ACK):
            rid = payload.get("request_id")
            if rid and rid in self._pending_acks:
                fut = self._pending_acks.pop(rid)
                if not fut.done():
                    fut.set_result(payload)
            return

        # HEALTH_PING → PONG
        if proto == Protocol.HEALTH_PING:
            await self._send_raw({
                "protocol_no":      Protocol.HEALTH_PONG,
                "protocol_version": self.PROTOCOL_VERSION,
                "request_id":       payload.get("request_id", ""),
                "timestamp":        self._now_iso(),
            })
            return

        # MODEL_RELOAD_CMD
        if proto == Protocol.MODEL_RELOAD_CMD:
            success, err = True, ""
            try:
                if self.on_model_reload:
                    await self.on_model_reload(payload)
            except Exception as e:
                success, err = False, str(e)
                logger.error(f"모델 리로드 실패: {e}")
            await self._send_raw({
                "protocol_no":      Protocol.MODEL_RELOAD_RES,
                "protocol_version": self.PROTOCOL_VERSION,
                "request_id":       payload.get("request_id", ""),
                "timestamp":        self._now_iso(),
                "success":          success,
                "error":            err,
            })

    # ──────────────────────────────────────
    # 패킷 송신
    # ──────────────────────────────────────

    async def _send_raw(self, payload: dict) -> bool:
        if not self._connected or not self._writer:
            logger.warning(f"메인서버 미연결 - 전송 스킵: {payload.get('protocol_no')}")
            return False
        try:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self._writer.write(struct.pack(">I", len(body)) + body)
            await self._writer.drain()
            return True
        except Exception as e:
            logger.error(f"전송 오류: {e}")
            await self._on_disconnect()
            return False

    async def _send_with_ack(self, payload: dict) -> bool:
        rid = payload["request_id"]
        for attempt in range(1, self.MAX_RETRY + 1):
            fut = asyncio.get_event_loop().create_future()
            self._pending_acks[rid] = fut
            if not await self._send_raw(payload):
                self._pending_acks.pop(rid, None)
                await asyncio.sleep(1.0)
                continue
            try:
                await asyncio.wait_for(fut, timeout=self.ACK_TIMEOUT)
                return True
            except asyncio.TimeoutError:
                logger.warning(f"ACK 타임아웃 ({attempt}/{self.MAX_RETRY})")
                self._pending_acks.pop(rid, None)
            except ConnectionError:
                await asyncio.sleep(1.0)
        logger.error(f"❌ 전송 실패 (3회): protocol_no={payload['protocol_no']}")
        return False

    # ──────────────────────────────────────
    # Public API
    # ──────────────────────────────────────

    async def push_focus_log(
        self,
        session_id:     int,
        focus_score:    int,
        state:          str,
        is_absent:      bool,
        is_drowsy:      bool,
        # 재학습용 7개 특징값
        ear:            float = 0.0,
        neck_angle:     float = 0.0,
        shoulder_diff:  float = 0.0,
        head_yaw:       float = 0.0,
        head_pitch:     float = 0.0,
        face_detected:  int   = 1,
        phone_detected: int   = 0,
        timestamp_ms:   Optional[int] = None,
    ) -> bool:
        """
        이벤트 로그 전송 (1000)
        distracted / drowsy 일 때만 호출

        추론 결과 + 7개 특징값을 함께 전송
        → 메인서버 DB에 저장 후 재학습용 CSV로 변환 가능
        """
        ts = timestamp_ms or self._now_ms()
        return await self._send_with_ack({
            "protocol_no":      Protocol.FOCUS_LOG_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       f"focus_{session_id}_{ts}_{uuid.uuid4().hex[:8]}",
            "timestamp":        self._now_iso(),
            "session_id":       session_id,
            "timestamp_ms":     ts,
            # 추론 결과
            "focus_score":      focus_score,
            "state":            state,
            "is_absent":        is_absent,
            "is_drowsy":        is_drowsy,
            # 재학습용 7개 특징값
            "ear":              round(float(ear), 4),
            "neck_angle":       round(float(neck_angle), 2),
            "shoulder_diff":    round(float(shoulder_diff), 2),
            "head_yaw":         round(float(head_yaw), 2),
            "head_pitch":       round(float(head_pitch), 2),
            "face_detected":    int(face_detected),
            "phone_detected":   int(phone_detected),
        })

    async def push_posture_event(
        self,
        session_id: int,
        event_type: str,
        severity:   str = "warning",
        reason:     str = "",
        timestamp_ms: Optional[int] = None,
    ) -> bool:
        """이벤트 전송 (1004) — 졸음/딴짓 이벤트"""
        ts  = timestamp_ms or self._now_ms()
        eid = f"evt_{session_id}_{ts}_{uuid.uuid4().hex[:8]}"
        return await self._send_with_ack({
            "protocol_no":      Protocol.POSTURE_EVENT_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       f"event_{eid}_{uuid.uuid4().hex[:8]}",
            "timestamp":        self._now_iso(),
            "event_id":         eid,
            "session_id":       session_id,
            "event_type":       event_type,
            "severity":         severity,
            "reason":           reason,
            "timestamp_ms":     ts,
        })

    # ──────────────────────────────────────
    # 종료
    # ──────────────────────────────────────

    async def close(self):
        for task in (self._reconnect_task, self._reader_task):
            if task:
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass
        await self._on_disconnect()
        logger.info("메인서버 클라이언트 종료")

    # ──────────────────────────────────────
    # 유틸
    # ──────────────────────────────────────

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now().timestamp() * 1000)

    @staticmethod
    def _now_iso() -> str:
        kst = timezone(timedelta(hours=9))
        return datetime.now(kst).isoformat(timespec="seconds")