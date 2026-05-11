"""
StudySync AI 서버 → 메인서버 TCP 통신 모듈
============================================
영구 연결, 자동 재연결, ACK 기반 재전송 지원

프로토콜:
    [4byte big-endian JSON 길이][JSON 페이로드]

프로토콜 번호 (메인서버와 합의):
    1000  AI → 메인  FOCUS_LOG_PUSH
    1001  메인 → AI  FOCUS_LOG_ACK
    1002  AI → 메인  POSTURE_LOG_PUSH
    1003  메인 → AI  POSTURE_LOG_ACK
    1004  AI → 메인  POSTURE_EVENT_PUSH
    1005  메인 → AI  POSTURE_EVENT_ACK
    1006  AI → 메인  BASELINE_CAPTURE_PUSH
    1007  메인 → AI  BASELINE_CAPTURE_ACK
    1010  메인 → AI  MODEL_RELOAD_CMD
    1011  AI → 메인  MODEL_RELOAD_RES
    1200  메인 → AI  HEALTH_PING (5초 주기)
    1201  AI → 메인  HEALTH_PONG

사용 예시:
    client = MainServerClient("10.10.10.130", 9000)
    await client.connect()
    await client.push_event(
        session_id=1001,
        event_type="bad_posture",
        severity="warning",
        reason="목 각도 35도 5초 지속",
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
    # AI → 메인 (push)
    FOCUS_LOG_PUSH        = 1000
    POSTURE_LOG_PUSH      = 1002
    POSTURE_EVENT_PUSH    = 1004
    BASELINE_CAPTURE_PUSH = 1006
    MODEL_RELOAD_RES      = 1011
    HEALTH_PONG           = 1201

    # 메인 → AI (ack/cmd)
    FOCUS_LOG_ACK         = 1001
    POSTURE_LOG_ACK       = 1003
    POSTURE_EVENT_ACK     = 1005
    BASELINE_CAPTURE_ACK  = 1007
    MODEL_RELOAD_CMD      = 1010
    HEALTH_PING           = 1200

    # protocol_no → ack_no 매핑 (재전송 판단용)
    PUSH_TO_ACK = {
        FOCUS_LOG_PUSH:        FOCUS_LOG_ACK,
        POSTURE_LOG_PUSH:      POSTURE_LOG_ACK,
        POSTURE_EVENT_PUSH:    POSTURE_EVENT_ACK,
        BASELINE_CAPTURE_PUSH: BASELINE_CAPTURE_ACK,
    }


# ──────────────────────────────────────────
# 메인서버 TCP 클라이언트
# ──────────────────────────────────────────

class MainServerClient:
    """
    AI 서버 → 메인서버 TCP 통신을 담당하는 클라이언트

    특징:
        - 영구 연결 유지
        - 끊김 시 2초 간격 자동 재연결
        - ACK 필요 메시지: 3초 타임아웃, 최대 3회 재전송
        - HEALTH_PING 수신 시 PONG 자동 응답
        - MODEL_RELOAD_CMD 수신 시 콜백 호출
    """

    PROTOCOL_VERSION    = "1.0"
    RECONNECT_INTERVAL  = 2.0       # 재연결 시도 간격(초)
    ACK_TIMEOUT         = 3.0       # ACK 대기 타임아웃(초)
    MAX_RETRY           = 3         # 최대 재전송 횟수

    def __init__(
        self,
        host: str,
        port: int,
        on_model_reload=None,
    ):
        """
        host: 메인서버 IP (예: "10.10.10.130")
        port: 메인서버 TCP 포트 (예: 9000)
        on_model_reload: MODEL_RELOAD_CMD 수신 시 호출될 비동기 콜백
        """
        self.host = host
        self.port = port
        self.on_model_reload = on_model_reload

        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._connected = False

        # request_id → asyncio.Future (ACK 대기용)
        self._pending_acks: dict[str, asyncio.Future] = {}

        # 백그라운드 태스크
        self._reader_task: Optional[asyncio.Task] = None
        self._reconnect_task: Optional[asyncio.Task] = None

    # ─────────────────────────────────────
    # 연결 관리
    # ─────────────────────────────────────

    async def connect(self):
        """초기 연결 시작 + 자동 재연결 태스크 가동"""
        self._reconnect_task = asyncio.create_task(self._reconnect_loop())
        # 첫 연결 시도가 완료될 때까지 잠시 대기
        for _ in range(10):
            if self._connected:
                return
            await asyncio.sleep(0.3)

    async def _reconnect_loop(self):
        """연결이 끊기면 2초 간격으로 재연결 시도"""
        while True:
            if not self._connected:
                try:
                    logger.info(f"메인서버 연결 시도: {self.host}:{self.port}")
                    self._reader, self._writer = await asyncio.open_connection(
                        self.host, self.port
                    )
                    self._connected = True
                    logger.info("✅ 메인서버 연결 성공")

                    # 수신 루프 시작
                    self._reader_task = asyncio.create_task(self._receive_loop())

                except (ConnectionRefusedError, OSError) as e:
                    logger.warning(f"메인서버 연결 실패: {e} - {self.RECONNECT_INTERVAL}초 후 재시도")

            await asyncio.sleep(self.RECONNECT_INTERVAL)

    async def _on_disconnect(self):
        """연결 끊김 처리"""
        if not self._connected:
            return

        logger.warning("⚠️  메인서버 연결 끊김 - 재연결 대기")
        self._connected = False

        # 대기 중인 ACK 모두 취소
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

        self._reader = None
        self._writer = None

    # ─────────────────────────────────────
    # 패킷 수신 루프
    # ─────────────────────────────────────

    async def _receive_loop(self):
        """메인서버로부터 패킷 수신 (ACK, PING, CMD 처리)"""
        try:
            while self._connected:
                # 4byte 길이 헤더 읽기
                length_bytes = await self._reader.readexactly(4)
                json_length = struct.unpack(">I", length_bytes)[0]

                # JSON 페이로드 읽기
                json_bytes = await self._reader.readexactly(json_length)
                payload = json.loads(json_bytes.decode("utf-8"))

                await self._dispatch(payload)

        except asyncio.IncompleteReadError:
            logger.warning("메인서버가 연결을 닫음")
        except Exception as e:
            logger.error(f"수신 루프 오류: {e}")
        finally:
            await self._on_disconnect()

    async def _dispatch(self, payload: dict):
        """수신 패킷 분기 처리"""
        proto_no = payload.get("protocol_no")

        # ACK 패킷 처리 → 대기 중인 Future 깨우기
        if proto_no in (
            Protocol.FOCUS_LOG_ACK,
            Protocol.POSTURE_LOG_ACK,
            Protocol.POSTURE_EVENT_ACK,
            Protocol.BASELINE_CAPTURE_ACK,
        ):
            request_id = payload.get("request_id")
            if request_id and request_id in self._pending_acks:
                fut = self._pending_acks.pop(request_id)
                if not fut.done():
                    fut.set_result(payload)
            return

        # 헬스체크 핑 → 퐁 응답
        if proto_no == Protocol.HEALTH_PING:
            await self._send_packet({
                "protocol_no":      Protocol.HEALTH_PONG,
                "protocol_version": self.PROTOCOL_VERSION,
                "request_id":       payload.get("request_id", self._make_request_id("pong")),
                "timestamp":        self._now_iso(),
            })
            return

        # 모델 리로드 명령
        if proto_no == Protocol.MODEL_RELOAD_CMD:
            success = True
            error_msg = ""
            try:
                if self.on_model_reload:
                    await self.on_model_reload(payload)
            except Exception as e:
                success = False
                error_msg = str(e)
                logger.error(f"모델 리로드 실패: {e}")

            await self._send_packet({
                "protocol_no":      Protocol.MODEL_RELOAD_RES,
                "protocol_version": self.PROTOCOL_VERSION,
                "request_id":       payload.get("request_id", self._make_request_id("model")),
                "timestamp":        self._now_iso(),
                "success":          success,
                "error":            error_msg,
            })
            return

        logger.debug(f"알 수 없는 protocol_no: {proto_no}")

    # ─────────────────────────────────────
    # 패킷 송신
    # ─────────────────────────────────────

    async def _send_packet(self, payload: dict) -> bool:
        """패킷 1개 전송 (4byte 길이 + JSON)"""
        if not self._connected or not self._writer:
            logger.warning(f"메인서버 미연결 상태 - 전송 스킵: protocol_no={payload.get('protocol_no')}")
            return False

        try:
            json_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            length_bytes = struct.pack(">I", len(json_bytes))

            self._writer.write(length_bytes + json_bytes)
            await self._writer.drain()
            return True

        except Exception as e:
            logger.error(f"전송 오류: {e}")
            await self._on_disconnect()
            return False

    async def _send_with_ack(self, payload: dict) -> bool:
        """ACK 필요한 메시지 전송 (3초 타임아웃, 최대 3회 재전송)"""
        request_id = payload["request_id"]

        for attempt in range(1, self.MAX_RETRY + 1):
            # ACK 대기용 Future 등록
            fut = asyncio.get_event_loop().create_future()
            self._pending_acks[request_id] = fut

            sent = await self._send_packet(payload)
            if not sent:
                self._pending_acks.pop(request_id, None)
                # 연결 끊긴 경우 재시도해도 의미없음 → 짧게 기다리고 재시도
                await asyncio.sleep(1.0)
                continue

            # ACK 대기
            try:
                await asyncio.wait_for(fut, timeout=self.ACK_TIMEOUT)
                return True   # ACK 도착 → 성공
            except asyncio.TimeoutError:
                logger.warning(
                    f"ACK 타임아웃 (시도 {attempt}/{self.MAX_RETRY}): "
                    f"protocol_no={payload['protocol_no']}, request_id={request_id}"
                )
                self._pending_acks.pop(request_id, None)
            except ConnectionError:
                logger.warning(f"ACK 대기 중 연결 끊김 (시도 {attempt}/{self.MAX_RETRY})")
                # 다음 시도 전 잠깐 대기
                await asyncio.sleep(1.0)

        logger.error(f"❌ 전송 실패 (3회 재시도 모두 실패): protocol_no={payload['protocol_no']}")
        return False

    # ─────────────────────────────────────
    # Public API: 데이터 push
    # ─────────────────────────────────────

    async def push_focus_log(
        self,
        session_id: int,
        focus_score: int,
        state: str,
        is_absent: bool,
        is_drowsy: bool,
        timestamp_ms: Optional[int] = None,
        ear: Optional[float] = None,
        neck_angle: Optional[float] = None,
        shoulder_diff: Optional[float] = None,
        head_yaw: Optional[float] = None,
        head_pitch: Optional[float] = None,
        face_detected: Optional[int] = None,
        phone_detected: Optional[int] = None,
    ) -> bool:
        """
        집중도 로그 전송 (1000)
        focus_logs 테이블 INSERT 용도
        """
        ts_ms = timestamp_ms or self._now_ms()
        payload = {
            "protocol_no":      Protocol.FOCUS_LOG_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       self._make_request_id(f"focus_{session_id}_{ts_ms}"),
            "timestamp":        self._now_iso(),
            "session_id":       session_id,
            "timestamp_ms":     ts_ms,
            "focus_score":      focus_score,
            "state":            state,           # "focus" | "distracted" | "drowsy"
            "is_absent":        is_absent,
            "is_drowsy":        is_drowsy,
        }
        if ear is not None:
            payload["ear"] = round(ear, 4)
        if neck_angle is not None:
            payload["neck_angle"] = round(neck_angle, 2)
        if shoulder_diff is not None:
            payload["shoulder_diff"] = round(shoulder_diff, 2)
        if head_yaw is not None:
            payload["head_yaw"] = round(head_yaw, 2)
        if head_pitch is not None:
            payload["head_pitch"] = round(head_pitch, 2)
        if face_detected is not None:
            payload["face_detected"] = int(face_detected)
        if phone_detected is not None:
            payload["phone_detected"] = int(phone_detected)
        return await self._send_with_ack(payload)

    async def push_posture_log(
        self,
        session_id: int,
        neck_angle: float,
        shoulder_diff: float,
        posture_ok: bool,
        vs_baseline: float = 0.0,
        timestamp_ms: Optional[int] = None,
    ) -> bool:
        """
        자세 로그 전송 (1002)
        posture_logs 테이블 INSERT 용도
        """
        ts_ms = timestamp_ms or self._now_ms()
        payload = {
            "protocol_no":      Protocol.POSTURE_LOG_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       self._make_request_id(f"posture_{session_id}_{ts_ms}"),
            "timestamp":        self._now_iso(),
            "session_id":       session_id,
            "timestamp_ms":     ts_ms,
            "neck_angle":       round(neck_angle, 2),
            "shoulder_diff":    round(shoulder_diff, 2),
            "posture_ok":       posture_ok,
            "vs_baseline":      round(vs_baseline, 4),
        }
        return await self._send_with_ack(payload)

    async def push_posture_event(
        self,
        session_id: int,
        event_type: str,                    # "bad_posture" | "drowsy" | "absent" | "rest_required"
        severity: str = "warning",          # "info" | "warning" | "critical"
        reason: str = "",
        timestamp_ms: Optional[int] = None,
        clip_id: Optional[str] = None,
        clip_access: str = "none",          # "none" | "local_only" | "uploaded_url"
        clip_ref: Optional[str] = None,
        clip_format: Optional[str] = None,
        frame_count: int = 0,
        retention_days: int = 3,
    ) -> bool:
        """
        자세/졸음/자리이탈 이벤트 전송 (1004)
        posture_events 테이블 INSERT 용도

        event_id는 중복 방지를 위해 UUID 기반으로 자동 생성
        """
        ts_ms = timestamp_ms or self._now_ms()
        event_id = f"evt_{session_id}_{ts_ms}_{uuid.uuid4().hex[:8]}"

        # 클립 만료 시각 계산
        expires_at_ms = None
        if clip_access != "none" and retention_days > 0:
            expires_at_ms = ts_ms + (retention_days * 86400 * 1000)

        payload = {
            "protocol_no":      Protocol.POSTURE_EVENT_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       self._make_request_id(f"event_{event_id}"),
            "timestamp":        self._now_iso(),
            "event_id":         event_id,
            "session_id":       session_id,
            "event_type":       event_type,
            "severity":         severity,
            "reason":           reason,
            "timestamp_ms":     ts_ms,
            "clip_id":          clip_id,
            "clip_access":      clip_access,
            "clip_ref":         clip_ref,
            "clip_format":      clip_format,
            "frame_count":      frame_count,
            "retention_days":   retention_days,
            "expires_at_ms":    expires_at_ms,
        }
        return await self._send_with_ack(payload)

    async def push_baseline_capture(
        self,
        session_id: int,
        neck_angle: float,
        shoulder_diff: float,
        ear: float,
        timestamp_ms: Optional[int] = None,
    ) -> bool:
        """기준 자세 캡처 결과 전송 (1006)"""
        ts_ms = timestamp_ms or self._now_ms()
        payload = {
            "protocol_no":      Protocol.BASELINE_CAPTURE_PUSH,
            "protocol_version": self.PROTOCOL_VERSION,
            "request_id":       self._make_request_id(f"baseline_{session_id}_{ts_ms}"),
            "timestamp":        self._now_iso(),
            "session_id":       session_id,
            "timestamp_ms":     ts_ms,
            "neck_angle":       round(neck_angle, 2),
            "shoulder_diff":    round(shoulder_diff, 2),
            "ear":              round(ear, 4),
        }
        return await self._send_with_ack(payload)

    # ─────────────────────────────────────
    # 종료 처리
    # ─────────────────────────────────────

    async def close(self):
        """클라이언트 종료 (재연결 중지 + 연결 닫기)"""
        if self._reconnect_task:
            self._reconnect_task.cancel()
            try:
                await self._reconnect_task
            except asyncio.CancelledError:
                pass

        if self._reader_task:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except asyncio.CancelledError:
                pass

        await self._on_disconnect()
        logger.info("메인서버 클라이언트 종료")

    # ─────────────────────────────────────
    # 유틸 함수
    # ─────────────────────────────────────

    @staticmethod
    def _now_ms() -> int:
        """현재 시각을 unix milliseconds로 반환"""
        return int(datetime.now().timestamp() * 1000)

    @staticmethod
    def _now_iso() -> str:
        """현재 시각을 ISO8601 (KST +09:00) 형식으로 반환"""
        kst = timezone(timedelta(hours=9))
        return datetime.now(kst).isoformat(timespec="seconds")

    @staticmethod
    def _make_request_id(prefix: str) -> str:
        """유니크한 request_id 생성"""
        return f"{prefix}_{uuid.uuid4().hex[:12]}"


# ──────────────────────────────────────────
# 단독 실행 테스트 (메인서버가 켜져 있을 때만)
# ──────────────────────────────────────────

async def _test():
    """테스트용: 메인서버에 샘플 이벤트 전송"""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

    client = MainServerClient(host="10.10.10.130", port=9000)
    await client.connect()

    if not client._connected:
        logger.error("메인서버 연결 실패 - 테스트 종료")
        return

    # 샘플 이벤트 전송
    ok = await client.push_posture_event(
        session_id=1001,
        event_type="bad_posture",
        severity="warning",
        reason="목 각도 35도 5초 지속",
    )
    logger.info(f"이벤트 전송 결과: {ok}")

    # 샘플 집중도 로그
    ok = await client.push_focus_log(
        session_id=1001,
        focus_score=72,
        state="focus",
        is_absent=False,
        is_drowsy=False,
    )
    logger.info(f"집중도 로그 전송 결과: {ok}")

    await client.close()


if __name__ == "__main__":
    asyncio.run(_test())
