"""TcpClient.py — 비동기 TCP 클라이언트

이 파일은 AI 추론 서버가 운용서버(메인 서버)와 TCP 통신하는 기능을 담당한다.

주요 기능:
  1. 연결 관리: 끊어지면 자동으로 재연결 시도
  2. ACK 기반 송신: NG 결과 같은 중요 메시지는 상대가 수신 확인(ACK)할 때까지 재전송
  3. Fire-and-forget 송신: OK 카운트 같은 비중요 메시지는 보내고 잊기
  4. 수신 처리: 백그라운드에서 ACK 응답, HEALTH_PING, MODEL_RELOAD 명령 처리

통신 흐름:
  추론서버 → 운용서버: NG 결과 전송 (ACK 대기)
  운용서버 → 추론서버: ACK, HEALTH_PING, MODEL_RELOAD_CMD
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import asyncio   # 비동기 I/O 프레임워크 (async/await)
import json      # JSON 인코딩/디코딩
import logging   # 로그 출력
import struct    # 바이너리 패킹 (4바이트 헤더)
from datetime import datetime, timezone  # 시간 처리 (UTC 타임스탬프)
from typing import Any, Callable, Optional  # 타입 힌트용

from Common.Packet import PacketBuilder  # 패킷 빌드 유틸리티
from Common.Protocol import ProtocolNo, expected_ack_no, requires_ack, PROTOCOL_VERSION


# 이 모듈 전용 로거
logger = logging.getLogger(__name__)


# ── ACK 타임아웃 / 재시도 정책 ──
# 요구사항: NG 전송 후 1초 내 ACK 미수신 시 최대 3회 재전송
ACK_TIMEOUT_SEC = 3.0    # ACK 응답 대기 시간 (초)
                         # v0.12.0 부터 MainServer 가 validate 후 즉시 ACK 를
                         # 발행(이미지 저장/DB 는 백그라운드) 하므로 1초도 충분하지만,
                         # 네트워크 지연/GC 여유를 고려해 3초 로 둔다.
MAX_SEND_ATTEMPTS = 3    # 최대 재전송 횟수


class TcpClient:
    """비동기 TCP 클라이언트.

    사용 흐름:
      1. TcpClient("192.168.0.10", 9000) — 객체 생성
      2. await send_with_ack(packet, ...) — ACK 필수 메시지 전송
      3. await send_fire_and_forget(packet) — 단순 전송
      4. 백그라운드에서 _run_receiver()가 수신 메시지를 자동 처리
    """

    def __init__(self, host: str, port: int, reconnect_delay_sec: float = 2.0):
        """TCP 클라이언트 초기화.

        Args:
            host: 운용서버 IP 주소 (예: "192.168.0.10")
            port: 운용서버 TCP 포트 (예: 9000)
            reconnect_delay_sec: 연결 실패 시 재시도 간격 (초)
        """
        self._host = host
        self._port = port
        self._reconnect_delay_sec = reconnect_delay_sec

        # asyncio 스트림 객체 (연결되면 채워짐)
        self._reader: Optional[asyncio.StreamReader] = None  # 수신용
        self._writer: Optional[asyncio.StreamWriter] = None  # 송신용

        # 동시 송신 방지 락 (여러 코루틴이 동시에 쓰면 패킷이 섞임)
        self._send_lock = asyncio.Lock()

        # ACK 대기 맵: inspection_id → Future 객체
        # NG를 보내면 여기에 Future를 등록하고, ACK가 오면 Future를 완료시킨다
        self._pending_acks: dict[str, asyncio.Future] = {}

        # 백그라운드 수신 코루틴 태스크
        self._receiver_task: Optional[asyncio.Task] = None

        # ── 콜백 ──
        self._on_model_reload: Optional[Callable[[dict], Any]] = None  # 모델 리로드 콜백
        # v0.14.0: INFERENCE_CONTROL_CMD(1020) 수신 콜백 — StationRunner 가 pause/resume 처리
        self._on_inference_control: Optional[Callable[[dict], Any]] = None
        self._station_id: int = 0  # 이 추론서버의 스테이션 번호

    def set_station_id(self, station_id: int) -> None:
        """스테이션 ID 설정 (HEALTH_PONG 응답에 포함됨).

        Args:
            station_id: 1 또는 2
        """
        self._station_id = station_id

    def set_on_model_reload(self, callback: Callable[[dict], Any]) -> None:
        """MODEL_RELOAD_CMD(1010) 수신 시 호출될 콜백 함수를 등록한다.

        Args:
            callback: 모델 리로드를 수행할 함수 (cmd_dict를 인자로 받음)
        """
        self._on_model_reload = callback

    def set_on_inference_control(self, callback: Callable[[dict], Any]) -> None:
        """INFERENCE_CONTROL_CMD(1020) 수신 시 호출될 콜백 (v0.14.0).

        Args:
            callback: pause/resume 를 수행할 함수 (cmd_dict["action"] = "pause"|"resume")
        """
        self._on_inference_control = callback

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    # 연결 관리
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    async def connect(self) -> None:
        """운용서버에 TCP 연결을 맺는다.

        연결 성공 시 백그라운드 수신 코루틴을 시작한다.
        """
        # asyncio.open_connection: 비동기 TCP 연결 → reader(수신), writer(송신) 반환
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        logger.info("TcpClient connected to %s:%d", self._host, self._port)

        # 수신 코루틴 시작 (아직 안 돌고 있으면)
        if self._receiver_task is None or self._receiver_task.done():
            self._receiver_task = asyncio.create_task(self._run_receiver())

    async def ensure_connected(self) -> None:
        """연결 상태를 확인하고, 끊어져 있으면 재연결한다.

        재연결이 성공할 때까지 무한 반복 (reconnect_delay_sec 간격)
        """
        # 이미 연결되어 있으면 즉시 반환
        if self._writer is not None and not self._writer.is_closing():
            return

        # 재연결 루프
        while True:
            try:
                await self.connect()
                return  # 연결 성공 → 루프 탈출
            except OSError as exc:
                logger.warning("connect failed: %s — retry in %.1fs",
                               exc, self._reconnect_delay_sec)
                await asyncio.sleep(self._reconnect_delay_sec)  # 잠시 대기 후 재시도

    async def close(self) -> None:
        """TCP 연결을 종료하고 수신 코루틴을 정리한다."""
        await self._discard_writer()  # 소켓 닫기

        # 수신 코루틴 취소
        if self._receiver_task is not None:
            self._receiver_task.cancel()
            try:
                await self._receiver_task
            except asyncio.CancelledError:
                pass  # 취소는 정상 동작

    async def _discard_writer(self) -> None:
        """writer(송신 소켓)를 안전하게 닫고 pending ACK를 모두 정리한다.

        연결이 끊긴 상태에서 계속 대기 중인 Future는 TimeoutError로 해제하여
        호출자가 깔끔하게 종료/재시도할 수 있게 한다.
        """
        if self._writer is not None:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None
            self._reader = None

        # 연결 끊김 — pending ACK 모두 취소 (orphan future 방지)
        if self._pending_acks:
            logger.warning("연결 끊김 | pending ACK %d건 정리", len(self._pending_acks))
            for inspection_id, fut in list(self._pending_acks.items()):
                if not fut.done():
                    fut.set_exception(ConnectionError("connection lost"))
            self._pending_acks.clear()

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    # 송신
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    async def send_with_ack(self,
                            packet_bytes: bytes,
                            protocol_no: int,
                            inspection_id: str) -> bool:
        """ACK 필수 메시지를 전송한다 (재전송 포함).

        1. 패킷 전송
        2. 1초간 ACK 대기
        3. ACK 미수신 시 재전송 (최대 3회)
        4. 3회 모두 실패하면 포기 (False 반환)

        모든 경로에서 _pending_acks를 확실히 정리하여 메모리 누수 방지.

        Args:
            packet_bytes: 전송할 패킷
            protocol_no: 메시지 번호
            inspection_id: 검사 ID (ACK 매칭 키)
        Returns:
            True: 전송 성공 (ACK 수신), False: 전송 실패
        """
        # ACK가 필요 없는 메시지면 단순 전송
        if not requires_ack(protocol_no):
            return await self._send_raw(packet_bytes)

        loop = asyncio.get_running_loop()

        try:
            for attempt in range(1, MAX_SEND_ATTEMPTS + 1):
                future: asyncio.Future = loop.create_future()
                self._pending_acks[inspection_id] = future

                try:
                    # 패킷 전송
                    ok = await self._send_raw(packet_bytes)
                    if not ok:
                        await asyncio.sleep(self._reconnect_delay_sec)
                        continue

                    # 1초간 ACK 대기
                    try:
                        ack_dict = await asyncio.wait_for(future, timeout=ACK_TIMEOUT_SEC)
                    except asyncio.TimeoutError:
                        logger.warning("ACK timeout inspection_id=%s attempt=%d/%d",
                                       inspection_id, attempt, MAX_SEND_ATTEMPTS)
                        continue

                    # ACK 수신 성공
                    if ack_dict.get("ack") is True:
                        logger.debug("ACK ok inspection_id=%s", inspection_id)
                        return True

                    # NACK(처리 실패) 수신 → 재시도 의미 없음, 포기
                    logger.error("NACK received inspection_id=%s err=%s",
                                 inspection_id, ack_dict.get("error_message"))
                    return False

                finally:
                    # 각 시도마다 확실히 pop — 예외 경로에서도 메모리 누수 방지
                    self._pending_acks.pop(inspection_id, None)

            # 3회 모두 실패
            logger.error("send_with_ack giveup inspection_id=%s after %d attempts",
                         inspection_id, MAX_SEND_ATTEMPTS)
            return False

        finally:
            # 함수 종료 시 최종 보장 — 예상치 못한 예외 시에도 정리
            self._pending_acks.pop(inspection_id, None)

    async def send_fire_and_forget(self, packet_bytes: bytes) -> bool:
        """ACK 없이 단순 전송한다 (OK 카운트, 메타데이터 등).

        Args:
            packet_bytes: 전송할 패킷
        Returns:
            True: 전송 성공, False: 전송 실패
        """
        return await self._send_raw(packet_bytes)

    async def _send_raw(self, packet_bytes: bytes) -> bool:
        """바이트를 TCP 소켓으로 전송한다 (저수준).

        동시 송신 방지를 위해 락을 사용한다.

        Args:
            packet_bytes: 전송할 바이트열
        Returns:
            True: 전송 성공, False: 전송 실패
        """
        async with self._send_lock:  # 한 번에 하나의 코루틴만 전송
            try:
                await self.ensure_connected()      # 연결 확인/재연결
                assert self._writer is not None    # 연결 보장
                self._writer.write(packet_bytes)   # 바이트를 송신 버퍼에 쓰기
                await self._writer.drain()         # 버퍼가 비워질 때까지 대기
                return True
            except (OSError, ConnectionError) as exc:
                logger.error("send failed: %s", exc)
                await self._discard_writer()       # 연결 해제
                return False

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    # 수신 (ACK 라우팅 + 명령 처리)
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    async def _run_receiver(self) -> None:
        """백그라운드에서 운용서버의 메시지를 수신하는 무한 루프.

        수신 처리:
          - HEALTH_PING(1200) → HEALTH_PONG(1201) 자동 응답
          - MODEL_RELOAD_CMD(1010) → 모델 리로드 콜백 실행 + 응답
          - ACK/NACK → inspection_id로 대기 중인 Future에 결과 전달
        """
        try:
            while True:
                # reader가 없으면 (아직 연결 안 됐거나 끊어짐) 잠시 대기
                if self._reader is None:
                    await asyncio.sleep(0.2)
                    continue

                try:
                    # 4바이트 헤더 읽기 (JSON 크기)
                    header = await self._reader.readexactly(4)
                except asyncio.IncompleteReadError:
                    # 연결이 끊어짐 (상대가 소켓을 닫음)
                    logger.warning("receiver: connection closed")
                    await self._discard_writer()
                    await asyncio.sleep(self._reconnect_delay_sec)
                    continue

                # 헤더에서 JSON 크기 추출 (big-endian 4바이트 정수)
                json_size = struct.unpack(">I", header)[0]

                # JSON 크기 제한: 최대 64KB (비정상 패킷 차단)
                if json_size == 0 or json_size > 64 * 1024:
                    logger.error("receiver: invalid json_size=%d — dropping connection", json_size)
                    await self._discard_writer()
                    await asyncio.sleep(self._reconnect_delay_sec)
                    continue

                # JSON 본문 읽기
                body = await self._reader.readexactly(json_size)
                try:
                    msg_dict = json.loads(body.decode("utf-8"))
                except json.JSONDecodeError as exc:
                    logger.error("receiver: invalid JSON: %s", exc)
                    continue

                # 바이너리 데이터 수신 (이미지 또는 모델 파일)
                image_size = int(msg_dict.get("image_size", 0))
                binary_data: bytes | None = None
                # 바이너리 크기 제한: 최대 500MB (모델 파일 포함)
                MAX_BINARY_SIZE = 500 * 1024 * 1024
                if image_size > MAX_BINARY_SIZE:
                    logger.error("receiver: binary too large=%d — dropping", image_size)
                    await self._discard_writer()
                    await asyncio.sleep(self._reconnect_delay_sec)
                    continue
                if image_size > 0:
                    binary_data = await self._reader.readexactly(image_size)

                # 메시지 번호로 분기 처리
                protocol_no = msg_dict.get("protocol_no", 0)

                # ── HEALTH_PING(1200) → HEALTH_PONG(1201) 자동 응답 ──
                if protocol_no == ProtocolNo.HEALTH_PING:
                    await self._handle_health_ping(msg_dict)
                    continue

                # ── MODEL_RELOAD_CMD(1010) → 모델 파일 저장 + 리로드 + 응답 ──
                if protocol_no == ProtocolNo.MODEL_RELOAD_CMD:
                    # 모델 바이너리가 있으면 cmd_dict에 첨부하여 콜백에 전달
                    if binary_data:
                        msg_dict["_model_bytes"] = binary_data
                    await self._handle_model_reload(msg_dict)
                    continue

                # ── INFERENCE_CONTROL_CMD(1020) → 검사 pause/resume + ACK ──
                # v0.14.0: 메인서버가 보낸 action="pause"/"resume" 에 따라
                # StationRunner 의 grab 이벤트 on/off.
                if protocol_no == ProtocolNo.INFERENCE_CONTROL_CMD:
                    await self._handle_inference_control(msg_dict)
                    continue

                # ── ACK/NACK 라우팅 ──
                # inspection_id로 대기 중인 Future를 찾아 결과를 전달
                inspection_id = msg_dict.get("inspection_id")
                if inspection_id and inspection_id in self._pending_acks:
                    fut = self._pending_acks.pop(inspection_id)  # 대기 맵에서 제거
                    if not fut.done():
                        fut.set_result(msg_dict)  # Future 완료 → send_with_ack에서 대기 해제
                else:
                    logger.debug("receiver: unmatched packet no=%s", protocol_no)

        except asyncio.CancelledError:
            raise  # 취소 요청은 그대로 전파
        except Exception as exc:
            logger.exception("receiver crashed: %s", exc)

    async def _handle_health_ping(self, ping_dict: dict) -> None:
        """HEALTH_PING(1200) 수신 → HEALTH_PONG(1201) 응답.

        운용서버가 5초마다 보내는 "살아있니?" 메시지에 "살아있다!" 응답.
        3회 무응답 시 운용서버가 이 추론서버를 '장애' 상태로 판정한다.

        Args:
            ping_dict: 수신한 HEALTH_PING 메시지 내용
        """
        # v0.15.0: server_type 필드 명시적 포함.
        #   ConnectionRegistry 의 server_type 태깅이 기존엔 station_id 로부터 유추되었으나,
        #   (Router 가 packet 내용으로 server_type 추론 → ai_inference_1/2 매칭)
        #   Protocol_README 에 명시된 대로 PONG 응답에 server_type 을 직접 실어 보내
        #   매칭 로직을 단순화하고 로그 추적성을 높인다.
        #   학습서버(TrainingMain._notify_recv_loop) 는 이미 v0.14.7 에서 동일 방식 적용됨.
        server_type = f"station{self._station_id}" if self._station_id in (1, 2) else "unknown"
        pong_body = {
            "station_id": self._station_id,  # 어느 스테이션인지
            "server_type": server_type,      # v0.15.0: "station1" / "station2"
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "status": "normal",  # 현재 상태 (normal / degraded)
            "queue_size": 0,     # 처리 대기 중인 큐 크기
        }
        pong_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.HEALTH_PONG),
            body_dict=pong_body,
        )
        await self._send_raw(pong_packet)
        logger.debug("HEALTH_PONG sent")

    async def _handle_model_reload(self, cmd_dict: dict) -> None:
        """MODEL_RELOAD_CMD(1010) 수신 → 모델 핫 리로드 + 응답.

        운용서버가 새 모델을 배포한 후 이 명령을 보내면,
        추론기가 새 모델 파일을 다시 로드한다 (서버 재시작 없이).

        Args:
            cmd_dict: 수신한 명령 내용 (model_path, version, request_id 포함)
        """
        request_id = cmd_dict.get("request_id", "")
        model_path = cmd_dict.get("model_path", "")
        version = cmd_dict.get("version", "")
        model_bytes: bytes | None = cmd_dict.pop("_model_bytes", None)

        # 모델 바이너리가 있으면 로컬에 원자적으로 저장 (임시파일 → rename)
        # 수신 도중 중단되거나 저장 실패 시 부분 파일이 남지 않도록 보장.
        if model_bytes:
            import os, time
            ext = os.path.splitext(model_path)[1] if model_path else ".bin"
            save_dir = os.path.join(".", "models")
            os.makedirs(save_dir, exist_ok=True)
            local_path = os.path.join(save_dir, f"{version}{ext}")
            # 임시파일: PID + 나노초로 동시 수신 충돌 방지
            tmp_path = f"{local_path}.tmp.{os.getpid()}.{time.time_ns()}"
            try:
                with open(tmp_path, "wb") as f:
                    f.write(model_bytes)
                    f.flush()
                    os.fsync(f.fileno())  # 디스크 쓰기 완료 보장

                # 저장 크기 검증
                if os.path.getsize(tmp_path) != len(model_bytes):
                    logger.error("모델 크기 불일치: 예상=%d 실제=%d",
                                 len(model_bytes), os.path.getsize(tmp_path))
                    os.remove(tmp_path)
                    raise ValueError("size mismatch")

                # atomic rename — 최종 경로로 이동
                os.replace(tmp_path, local_path)
                logger.info("모델 파일 저장 완료: %s (%d bytes)",
                            local_path, len(model_bytes))
                cmd_dict["model_path"] = local_path
            except Exception as exc:
                logger.error("모델 파일 저장 실패: %s", exc)
                # 실패 시 임시파일 정리 (v0.15.0: except: pass 제거)
                #   수백 MB 모델 파일의 임시본이 남으면 디스크가 조용히 소진되므로
                #   정리 실패 자체를 WARN 로그로 추적할 수 있게 한다. 예외 종류와
                #   경로까지 기록해 운영자가 수동 정리 가능.
                if os.path.exists(tmp_path):
                    try:
                        os.remove(tmp_path)
                    except OSError as rm_exc:
                        logger.warning("임시파일 정리 실패 path=%s err=%s",
                                       tmp_path, rm_exc)

        success = False
        if self._on_model_reload is not None:
            try:
                self._on_model_reload(cmd_dict)
                success = True
                logger.info("모델 리로드 성공: path=%s version=%s", model_path, version)
            except Exception as exc:
                logger.error("모델 리로드 실패: %s", exc)
        else:
            logger.warning("모델 리로드 콜백 미등록")

        # MODEL_RELOAD_RES(1011) 응답: 성공/실패 결과를 운용서버에 알림
        res_body = {
            "station_id": self._station_id,
            "success": success,
            "version": version,
        }
        res_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.MODEL_RELOAD_RES),
            body_dict=res_body,
            request_id=request_id,
        )
        await self._send_raw(res_packet)

    async def _handle_inference_control(self, cmd_dict: dict) -> None:
        """INFERENCE_CONTROL_CMD(1020) 수신 → pause/resume + ACK(1021) (v0.14.0).

        메인서버가 클라 요청(INSPECT_CONTROL_REQ 160)을 릴레이해 오면,
        StationRunner 에 등록된 콜백이 실제 pause/resume 을 적용한다.
        ACK 로 현재 상태(paused: true/false)를 회신한다.

        Args:
            cmd_dict: {action: "pause"|"resume", request_id: ...}
        """
        request_id = cmd_dict.get("request_id", "")
        action = cmd_dict.get("action", "")

        paused_now = False
        ok = False
        msg = ""

        if self._on_inference_control is not None:
            try:
                # 콜백 반환값으로 현재 paused 상태를 받음 (True/False)
                result = self._on_inference_control(cmd_dict)
                if isinstance(result, bool):
                    paused_now = result
                ok = True
                logger.info("검사 %s 적용 완료 | paused=%s", action, paused_now)
            except Exception as exc:
                msg = f"control callback failed: {exc}"
                logger.error(msg)
        else:
            msg = "control callback not registered"
            logger.warning(msg)

        res_body = {
            "station_id": self._station_id,
            "action": action,
            "paused": paused_now,
            "success": ok,
            "message": msg,
        }
        res_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.INFERENCE_CONTROL_RES),
            body_dict=res_body,
            request_id=request_id,
        )
        await self._send_raw(res_packet)
