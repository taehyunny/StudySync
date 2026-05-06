"""TrainingMain.py
AI 학습 서버 진입점 (Entry Point).

이 파일의 역할:
  이 파일은 AI 학습 서버의 메인 프로그램으로, 프로그램 실행 시 가장 먼저 호출된다.
  TCP 서버를 열어 운용서버의 명령을 수신하고, 학습 요청이 오면 PatchCore 또는 YOLO11 학습을 실행한다.
  학습 진행률과 완료/실패 결과를 운용서버에 알려주는 역할도 한다.

프로토콜 (운용서버와 주고받는 메시지 종류):
  - TRAIN_START_REQ(1100)  : 운용서버가 학습 시작을 요청한다
  - TRAIN_START_RES(1101)  : 학습 서버가 요청을 수락/거부 응답한다
  - TRAIN_PROGRESS(1102)   : 학습 진행률을 주기적으로 운용서버에 전송한다
  - TRAIN_COMPLETE(1104)   : 학습 성공 완료를 운용서버에 알린다
  - TRAIN_FAIL(1106)       : 학습 실패를 운용서버에 알린다
  - HEALTH_PING(1200)      : 운용서버가 학습 서버가 살아있는지 확인하는 핑
  - HEALTH_PONG(1201)      : 학습 서버가 핑에 대해 응답하는 퐁

실행 방법:
  cd Factory/AiServer
  python -m Training.TrainingMain
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가한다.
from __future__ import annotations

# asyncio: 비동기(async/await) 프로그래밍을 위한 표준 라이브러리이다.
# 여러 클라이언트의 요청을 동시에 처리할 수 있게 해준다.
# 스레드 없이도 동시성을 구현할 수 있어 효율적이다.
import asyncio

# json: JSON 형식의 데이터를 파이썬 딕셔너리로 변환(파싱)하거나 그 반대를 수행한다.
# 서버 간 통신에서 메시지 본문(body)을 JSON 형식으로 주고받는다.
import json

# logging: 프로그램 실행 중 상태 메시지를 기록하는 표준 라이브러리이다.
import logging

# signal: 운영체제 시그널(Ctrl+C 등)을 처리한다.
# 프로그램을 안전하게 종료(graceful shutdown)할 때 사용한다.
import signal

# struct: 바이트 데이터를 파이썬 자료형으로 변환(패킹/언패킹)한다.
# TCP 통신에서 4바이트 헤더(메시지 길이)를 읽을 때 사용한다.
import struct

# threading: 스레드를 생성하고 관리한다 (이 파일에서는 직접 사용하지 않지만 임포트되어 있다).
import threading

# datetime: 날짜/시간 클래스. timezone: UTC 등 시간대 정보를 다룬다.
from datetime import datetime, timezone

# Path: 파일 경로를 객체지향적으로 다루는 클래스이다.
from pathlib import Path

# Optional: None이 될 수 있는 타입을 표현한다.
from typing import Optional

# sys: 파이썬 인터프리터와 상호작용하는 모듈이다. 모듈 검색 경로(sys.path) 수정에 사용한다.
import sys

# sys.path에 상위 폴더를 추가한다.
# 이유: "python -m Training.TrainingMain"으로 실행할 때, Common 패키지를 찾을 수 있게 하기 위해서이다.
# __file__: 현재 파일의 경로, resolve(): 절대 경로로 변환, parent.parent: 2단계 상위 폴더 (AiServer/)
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# PacketBuilder: TCP 통신에 사용하는 패킷(헤더 + JSON 본문)을 만들어주는 유틸리티 클래스이다.
from Common.Packet import PacketBuilder

# ProtocolNo: 프로토콜 번호를 정의한 열거형(Enum)이다 (예: TRAIN_START_REQ = 1100).
# PROTOCOL_VERSION: 통신 프로토콜 버전 문자열이다.
from Common.Protocol import ProtocolNo, PROTOCOL_VERSION

# TrainingConfig: 학습 서버 설정을 담는 데이터 클래스이다.
from Training.TrainingConfig import TrainingConfig

# PatchcoreTrainer: PatchCore 학습을 수행하는 클래스이다.
# augment_dataset: 데이터 증강 함수이다.
from Training.TrainPatchcore import PatchcoreTrainer, augment_dataset

# YoloTrainer: YOLO11 학습을 수행하는 클래스이다.
# create_data_yaml: YOLO 학습용 data.yaml 파일 생성 함수이다.
from Training.TrainYolo import YoloTrainer, create_data_yaml


# 로깅 설정: 로그 출력 형식과 최소 로그 레벨을 지정한다.
logging.basicConfig(
    level=logging.INFO,  # INFO 레벨 이상(INFO, WARNING, ERROR)의 로그만 출력한다
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",  # 로그 형식: 시간 [레벨] 모듈: 메시지
)

# 이 모듈 전용 로거를 생성한다.
logger = logging.getLogger(__name__)


class TrainingServer:
    """AI 학습 서버 메인 클래스.

    용도:
      TCP 서버를 열어 운용서버의 학습 명령을 수신하고,
      PatchCore 또는 YOLO11 학습을 비동기적으로 실행한다.
      학습 진행률과 결과를 운용서버에 전송한다.

    매개변수:
      config (TrainingConfig): 서버 설정 객체 (IP, 포트, GPU 설정, 하이퍼파라미터 등)
    """

    def __init__(self, config: TrainingConfig):
        """TrainingServer 생성자.

        매개변수:
          config (TrainingConfig): 학습 서버의 전체 설정을 담은 객체

        반환값:
          없음 (생성자이므로 인스턴스를 반환)
        """
        self._config = config                # 설정 객체를 저장한다
        self._is_running = False             # 서버 실행 상태 플래그 (True: 실행 중)
        self._training_lock = asyncio.Lock()  # 비동기 잠금: 동시에 여러 학습이 실행되지 않도록 보호한다
        self._current_training: Optional[str] = None  # 현재 학습 중인 작업 ID (없으면 None = 유휴 상태)

        # 운용서버로 알림을 전송하기 위한 TCP 클라이언트 연결 객체이다.
        # StreamWriter: 데이터를 보내는 객체, StreamReader: 데이터를 받는 객체
        self._notify_writer: Optional[asyncio.StreamWriter] = None  # 운용서버에 데이터를 보내는 writer
        self._notify_reader: Optional[asyncio.StreamReader] = None  # 운용서버에서 데이터를 받는 reader

    async def run(self) -> None:
        """TCP 서버를 시작하고 클라이언트 연결을 대기한다.

        용도:
          학습 서버의 메인 루프이다.
          1) 운용서버에 알림 전송용 TCP 연결을 맺는다.
          2) TCP 서버를 열어 운용서버의 명령을 대기한다.

        매개변수:
          없음

        반환값:
          없음 (None). 서버가 종료될 때까지 계속 실행된다.
        """
        # 서버 실행 상태를 True로 설정한다.
        self._is_running = True

        # 운용서버에 연결하는 작업을 백그라운드 태스크로 시작한다.
        # create_task(): 코루틴을 비동기 태스크로 등록한다 (별도로 실행됨).
        asyncio.create_task(self._connect_to_main_server())

        # TCP 서버를 시작한다. 클라이언트가 접속하면 _handle_client가 자동 호출된다.
        server = await asyncio.start_server(
            self._handle_client,        # 새 클라이언트 접속 시 호출될 핸들러 함수
            self._config.listen_host,   # 수신할 IP 주소 (기본: 0.0.0.0 = 모든 인터페이스)
            self._config.listen_port,   # 수신할 포트 번호 (기본: 9100)
        )

        # 실제로 바인딩된 주소를 가져와서 로그에 출력한다.
        addr = server.sockets[0].getsockname()  # (IP, 포트) 튜플
        logger.info("Training Server listening on %s:%d", addr[0], addr[1])

        # 서버를 영구 실행한다. Ctrl+C 등으로 중단될 때까지 계속 클라이언트를 수신한다.
        async with server:
            await server.serve_forever()

    async def _connect_to_main_server(self) -> None:
        """운용서버에 TCP 연결을 맺는다 (학습 진행/완료 알림 전송용).

        용도:
          학습 진행률, 학습 완료, 학습 실패 등의 알림을 운용서버에 보내려면
          먼저 TCP 연결이 필요하다. 이 함수가 연결을 담당한다.
          연결에 실패하면 5초 후 재시도한다 (운용서버가 아직 시작되지 않았을 수 있으므로).

        v0.14.7: 연결 성공 후 **수신 루프도 함께 시작**.
          메인서버가 이 채널로 HEALTH_PING(1200) 을 보내면 HEALTH_PONG(1201)+
          server_type="training" 으로 응답해야 ConnectionRegistry 가 태깅 →
          HealthChecker 가 "ai_training" 으로 인식 → LED 초록색 전환.
          이전엔 send-only 였어서 ping 을 무시 → LED 영원히 회색으로 남던 버그.

        매개변수:
          없음

        반환값:
          없음 (None). 연결 성공 시 self._notify_writer/reader에 저장한다.
        """
        # 서버가 실행 중인 동안 계속 재시도한다.
        while self._is_running:
            try:
                # asyncio.open_connection(): 비동기적으로 TCP 연결을 맺는다.
                # 반환값: (StreamReader, StreamWriter) 튜플
                self._notify_reader, self._notify_writer = await asyncio.open_connection(
                    self._config.main_server_host,   # 운용서버 IP
                    self._config.main_server_port,   # 운용서버 포트
                )
                # 연결 성공 로그를 남긴다.
                logger.info("Connected to main server %s:%d for notifications",
                            self._config.main_server_host, self._config.main_server_port)
                # v0.14.7: 수신 루프 백그라운드 태스크로 시작 (HEALTH_PING 응답용)
                asyncio.create_task(self._notify_recv_loop())
                return  # 연결 성공 시 함수 종료 (재시도 루프 탈출)
            except OSError as exc:
                # 연결 실패 시 (운용서버가 아직 안 떴거나 네트워크 오류)
                logger.warning("Main server connection failed: %s — retry in 5s", exc)
                await asyncio.sleep(5.0)  # 5초 대기 후 재시도

    async def _notify_recv_loop(self) -> None:
        """메인서버 → 학습서버(notify 채널) 수신 루프 (v0.14.7).

        메인서버의 HealthChecker 가 주기적으로 HEALTH_PING(1200) 을 이 채널로
        쏘는데, 이전엔 아무 처리도 안 해서 서버가 "ai_training" 태깅을 받지 못했다.
        이제 패킷을 파싱해 HEALTH_PING 이면 HEALTH_PONG(server_type="training") 을
        **같은 채널로** 즉시 회신 → Router 가 태깅 → HealthChecker 생존 판정 → LED 초록.

        종료 조건:
          - 서버 자체가 종료 (_is_running=False)
          - reader EOF (main 연결 끊김) → 연결 끊김 처리 후 루프 탈출
            (_send_to_main 이 다음 패킷 전송 시 재접속 함)
        """
        if self._notify_reader is None:
            return
        try:
            while self._is_running:
                # 4바이트 헤더 읽기
                header = await self._notify_reader.readexactly(4)
                body_size = int.from_bytes(header, "big")
                if body_size <= 0 or body_size > 1024 * 1024:
                    logger.warning("notify 수신 비정상 크기: %d — 루프 종료", body_size)
                    return
                body = await self._notify_reader.readexactly(body_size)
                try:
                    msg = json.loads(body.decode("utf-8"))
                except Exception as exc:
                    logger.warning("notify 수신 JSON 파싱 실패: %s", exc)
                    continue

                protocol_no = msg.get("protocol_no")
                if protocol_no == int(ProtocolNo.HEALTH_PING):
                    # HEALTH_PONG 조립 후 동일 채널(_notify_writer) 로 회신
                    pong_body = {
                        "server_type": "training",
                        "timestamp": datetime.utcnow().isoformat() + "Z",
                    }
                    pong_pkt = PacketBuilder.build_packet(
                        protocol_no=int(ProtocolNo.HEALTH_PONG),
                        body_dict=pong_body,
                    )
                    if self._notify_writer is not None and not self._notify_writer.is_closing():
                        self._notify_writer.write(pong_pkt)
                        await self._notify_writer.drain()
                # 그 외 패킷은 무시 (TRAIN_START_REQ 등은 listen_port 채널에서 처리)
        except asyncio.IncompleteReadError:
            logger.info("notify 수신 EOF — main 연결 끊김, _send_to_main 이 재연결 시도")
        except Exception as exc:
            logger.warning("notify 수신 루프 예외: %s — 루프 종료", exc)

    async def _send_to_main(self, packet: bytes) -> bool:
        """운용서버로 패킷(바이트 데이터)을 전송한다.

        용도:
          학습 진행률, 완료, 실패 알림 패킷을 운용서버로 보낸다.
          연결이 끊어져 있으면 자동으로 재연결을 시도한다.

        매개변수:
          packet (bytes): 전송할 패킷 데이터 (헤더 + JSON 본문)

        반환값 (bool):
          True: 전송 성공, False: 전송 실패
        """
        # writer가 없거나 연결이 닫혀있으면 재연결을 시도한다.
        if self._notify_writer is None or self._notify_writer.is_closing():
            await self._connect_to_main_server()
        try:
            if self._notify_writer is not None:
                # writer.write(): 전송 버퍼에 데이터를 쓴다 (아직 실제 전송은 아님).
                self._notify_writer.write(packet)
                # writer.drain(): 버퍼의 데이터가 실제로 전송될 때까지 기다린다.
                await self._notify_writer.drain()
                return True  # 전송 성공
        except (OSError, ConnectionError) as exc:
            # 전송 실패 시 (연결 끊김 등) 에러 로그를 남기고 writer를 초기화한다.
            logger.error("Send to main server failed: %s", exc)
            self._notify_writer = None  # 다음 전송 시 재연결 시도하도록 None으로 설정
        return False  # 전송 실패

    # ── TCP 클라이언트 핸들러 ──

    async def _handle_client(self, reader: asyncio.StreamReader,
                             writer: asyncio.StreamWriter) -> None:
        """운용서버로부터 TCP 연결이 들어오면 호출되는 핸들러 함수.

        용도:
          운용서버가 보내는 메시지(학습 시작 요청, 헬스체크 등)를 수신하고 처리한다.
          TCP 패킷 구조: [4바이트 헤더(JSON 크기)] + [JSON 본문] + [이미지 데이터(선택)]

        매개변수:
          reader (asyncio.StreamReader): 클라이언트에서 데이터를 읽는 스트림
          writer (asyncio.StreamWriter): 클라이언트에 데이터를 쓰는 스트림

        반환값:
          없음 (None)
        """
        # 접속한 클라이언트의 IP 주소와 포트를 가져온다.
        addr = writer.get_extra_info("peername")
        logger.info("Client connected: %s", addr)

        try:
            # 무한 루프로 클라이언트의 메시지를 계속 수신한다.
            while True:
                # ── 1단계: 4바이트 헤더 읽기 ──
                # 헤더에는 JSON 본문의 크기(바이트 수)가 빅엔디안(Big-Endian) 4바이트 정수로 들어있다.
                header = await reader.readexactly(4)  # 정확히 4바이트를 읽는다

                # struct.unpack(">I", header): 빅엔디안(>) unsigned int(I) 형식으로 언패킹한다.
                # [0]: unpack은 튜플을 반환하므로 첫 번째 원소를 꺼낸다.
                json_size = struct.unpack(">I", header)[0]

                # ── 2단계: JSON 본문 읽기 ──
                # 헤더에서 읽은 크기만큼 바이트를 더 읽는다.
                body = await reader.readexactly(json_size)

                # 바이트 데이터를 UTF-8 문자열로 디코딩한 후, JSON 파싱하여 딕셔너리로 변환한다.
                msg = json.loads(body.decode("utf-8"))

                # ── 3단계: 이미지 바이너리 수신 (있으면) ──
                # JSON 의 image_size 만큼 뒤에 바이너리가 붙어있다.
                # 프로토콜별로 쓰임이 달라 여기서는 그냥 "읽기만" 하고 bytes 로 보관
                # → TRAIN_DATA_UPLOAD(1108) 같은 프로토콜이 실제 파일 바이트로 사용.
                image_size = int(msg.get("image_size", 0))
                image_bytes: bytes = b""
                if image_size > 0:
                    image_bytes = await reader.readexactly(image_size)

                # JSON에서 protocol_no(프로토콜 번호)를 꺼낸다.
                protocol_no = msg.get("protocol_no", 0)

                # ── 4단계: 프로토콜 번호에 따라 분기 처리 ──
                if protocol_no == ProtocolNo.HEALTH_PING:
                    # 헬스체크 핑 -> 퐁 응답
                    await self._handle_health_ping(writer, msg)
                elif protocol_no == ProtocolNo.TRAIN_START_REQ:
                    # 학습 시작 요청 -> 학습 시작 처리
                    await self._handle_train_start(writer, msg)
                elif protocol_no == ProtocolNo.TRAIN_DATA_UPLOAD:
                    # v0.13.0: 메인서버로부터 학습용 이미지 1장 수신 → 디스크 저장 + ACK
                    await self._handle_train_data_upload(writer, msg, image_bytes)
                else:
                    # 알 수 없는 프로토콜 번호 -> 디버그 로그만 남김
                    logger.debug("Unknown protocol_no: %d", protocol_no)

        except asyncio.IncompleteReadError:
            # 클라이언트가 연결을 끊으면 readexactly()에서 이 에러가 발생한다.
            # 정상적인 연결 종료이므로 INFO 레벨로 로그를 남긴다.
            logger.info("Client disconnected: %s", addr)
        except Exception as exc:
            # 그 외 예상치 못한 에러는 스택 트레이스와 함께 기록한다.
            logger.exception("Client handler error: %s", exc)
        finally:
            # 반드시 writer를 닫아서 TCP 연결을 정리한다.
            # finally 블록은 예외 발생 여부와 관계없이 항상 실행된다.
            writer.close()
            try:
                # wait_closed(): writer가 완전히 닫힐 때까지 기다린다.
                await writer.wait_closed()
            except Exception:
                # 이미 닫힌 경우 등의 에러는 무시한다.
                pass

    # ── TRAIN_DATA_UPLOAD 처리 (v0.13.0: 클라이언트 업로드 이미지 저장) ──

    async def _handle_train_data_upload(self, writer: asyncio.StreamWriter,
                                         msg: dict, image_bytes: bytes) -> None:
        """TRAIN_DATA_UPLOAD(1108) 수신 → 파일 저장 + ACK(1109) 회신.

        JSON 필드:
          session_id: 업로드 세션 식별자 (동일 세션의 파일들을 한 폴더에 모음)
          station_id: 1 or 2 — 저장 경로 분기
          model_type: "PatchCore" / "YOLO11" — 저장 경로 분기
          filename:   원본 파일명 (path traversal 방지용 basename 화)
          image_size: 뒤따르는 바이너리 크기

        저장 경로:
          ./data/station{N}/uploads/{session_id}/{sanitized_filename}

        동일 session_id 로 여러 번 호출 가능 (한 장씩 누적 저장).
        _handle_train_start 가 나중에 data_path 로 이 폴더를 지정받아 학습 실행.
        """
        request_id  = msg.get("request_id", "")
        session_id  = msg.get("session_id", "")
        station_id  = int(msg.get("station_id", 0))
        model_type  = msg.get("model_type", "")
        filename    = msg.get("filename", "")

        # 기본 검증: 세션 ID 필수, station 1/2 만, 파일명 traversal 차단
        ok = True
        err_msg = ""
        saved_path = ""

        if not session_id or station_id not in (1, 2) or not filename:
            ok = False
            err_msg = "invalid_upload_request"
        else:
            # basename 화로 경로 탐색 공격 차단 + 확장자 허용 목록
            safe_name = Path(filename).name
            if ".." in safe_name or "/" in safe_name or "\\" in safe_name:
                ok = False
                err_msg = "invalid_filename"
            elif len(image_bytes) == 0:
                ok = False
                err_msg = "empty_image"
            elif len(image_bytes) > 50 * 1024 * 1024:
                ok = False
                err_msg = "image_too_large"
            else:
                # station/model_type 별 폴더 분기
                #   Station1: PatchCore 만 → station1/uploads/{session}/
                #   Station2: YOLO/PatchCore 구분 위해 세부 폴더 추가 가능하나, 여기서는 통일
                upload_dir = Path(self._config.data_root) / f"station{station_id}" \
                             / "uploads" / session_id
                try:
                    upload_dir.mkdir(parents=True, exist_ok=True)
                    dst = upload_dir / safe_name
                    dst.write_bytes(image_bytes)
                    saved_path = str(dst)
                    logger.info("업로드 이미지 저장 | session=%s station=%d type=%s file=%s (%d bytes)",
                                session_id, station_id, model_type, safe_name, len(image_bytes))
                except Exception as exc:
                    ok = False
                    err_msg = f"save_failed: {exc}"
                    logger.error("업로드 이미지 저장 실패 | %s", exc)

        # ACK 회신 (1109) — JSON 만 (바이너리 없음)
        ack_body = {
            "session_id": session_id,
            "success": ok,
            "saved_path": saved_path,
            "message": err_msg,
        }
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_DATA_UPLOAD_ACK),
            body_dict=ack_body,
            request_id=request_id,
        )
        writer.write(packet)
        await writer.drain()

    # ── HEALTH_PING 응답 ──

    async def _handle_health_ping(self, writer: asyncio.StreamWriter,
                                  ping_msg: dict) -> None:
        """HEALTH_PING(1200) 메시지에 대해 HEALTH_PONG(1201)으로 응답한다.

        용도:
          운용서버가 "학습 서버 살아있니?"라고 물으면 "네, 살아있어요"라고 답한다.
          현재 학습 중인지(training) 아닌지(idle)도 함께 알려준다.

        매개변수:
          writer (asyncio.StreamWriter): 응답을 보낼 클라이언트 스트림
          ping_msg (dict): 수신한 PING 메시지 (여기서는 사용하지 않지만 파라미터로 받음)

        반환값:
          없음 (None)
        """
        # 응답 본문(body)을 딕셔너리로 구성한다.
        pong_body = {
            "server_type": "training",  # 서버 종류: 학습 서버
            # 현재 UTC 시각을 ISO 8601 형식 문자열로 변환한다.
            # 예: "2026-04-16T05:30:52.123Z"
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            # 현재 상태: 학습 중이면 "training", 아니면 "idle"
            "status": "training" if self._current_training else "idle",
            # 현재 학습 중인 작업 ID (없으면 빈 문자열)
            "current_task": self._current_training or "",
        }

        # PacketBuilder로 HEALTH_PONG 패킷을 만든다.
        # 패킷 구조: [4바이트 헤더] + [JSON 본문]
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.HEALTH_PONG),  # 프로토콜 번호 1201
            body_dict=pong_body,                       # 응답 본문 딕셔너리
        )

        # 클라이언트(운용서버)에게 응답 패킷을 전송한다.
        writer.write(packet)
        await writer.drain()  # 실제 전송 완료까지 대기

    # ── 학습 시작 명령 ──

    async def _handle_train_start(self, writer: asyncio.StreamWriter,
                                  msg: dict) -> None:
        """TRAIN_START_REQ(1100) 메시지를 처리한다.

        용도:
          운용서버의 학습 시작 요청을 받아서:
          1) 이미 학습 중이면 거부 응답을 보낸다.
          2) 학습 가능하면 수락 응답을 보내고, 백그라운드에서 학습을 시작한다.

        매개변수:
          writer (asyncio.StreamWriter): 응답을 보낼 클라이언트 스트림
          msg (dict): 수신한 학습 시작 요청 메시지. 포함 필드:
            - request_id: 요청 고유 ID
            - station_id: 스테이션 번호 (1 또는 2)
            - model_type: 모델 종류 ("PatchCore" 또는 "YOLO11")
            - data_path: 학습 데이터 경로

        반환값:
          없음 (None)
        """
        # 요청 메시지에서 필요한 정보를 추출한다.
        request_id = msg.get("request_id", "")     # 요청 고유 ID (응답에 포함시킬 용도)
        station_id = msg.get("station_id", 1)      # 스테이션 번호 (기본값: 1)
        model_type = msg.get("model_type", "PatchCore")  # 모델 종류 (기본값: PatchCore)
        data_path = msg.get("data_path", "")       # 학습 데이터 경로 (빈 문자열이면 기본 경로 사용)

        # ── 이미 학습 중인지 확인 ──
        if self._current_training is not None:
            # 이미 다른 학습이 진행 중이면 거부 응답을 보낸다.
            # 동시에 두 개의 학습을 실행하면 GPU 메모리 부족이 발생할 수 있기 때문이다.
            res_body = {
                "success": False,  # 거부
                "message": f"Already training: {self._current_training}",  # 현재 진행 중인 작업 ID 알림
            }
            # TRAIN_START_RES(1101) 패킷을 만들어 전송한다.
            res_packet = PacketBuilder.build_packet(
                protocol_no=int(ProtocolNo.TRAIN_START_RES),
                body_dict=res_body,
                request_id=request_id,  # 원래 요청의 ID를 포함시켜 어떤 요청에 대한 응답인지 식별
            )
            writer.write(res_packet)
            await writer.drain()
            return  # 여기서 함수 종료 (학습 시작 안 함)

        # ── 학습 수락 응답 ──
        res_body = {"success": True, "message": "Training accepted"}
        res_packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_START_RES),  # 프로토콜 번호 1101
            body_dict=res_body,
            request_id=request_id,
        )
        # 수락 응답을 먼저 보낸다 (학습은 오래 걸리므로 먼저 "알겠다"고 응답).
        writer.write(res_packet)
        await writer.drain()

        # ── 비동기 학습 태스크 생성 ──
        # 작업 ID를 생성한다. 예: "station1_PatchCore_abc123"
        task_id = f"station{station_id}_{model_type}_{request_id}"

        # asyncio.create_task(): 코루틴을 백그라운드 태스크로 등록한다.
        # 이렇게 하면 학습이 진행되는 동안에도 서버가 다른 요청(헬스체크 등)을 처리할 수 있다.
        asyncio.create_task(
            self._run_training(task_id, station_id, model_type, data_path, request_id)
        )

    async def _run_training(self, task_id: str, station_id: int,
                            model_type: str, data_path: str,
                            request_id: str) -> None:
        """학습을 실행하는 백그라운드 태스크.

        용도:
          model_type에 따라 PatchCore 또는 YOLO11 학습을 실행하고,
          완료/실패 결과를 운용서버에 알린다.
          학습은 CPU 집약적이므로 run_in_executor()를 사용하여 별도 스레드에서 실행한다.

        매개변수:
          task_id (str): 작업 고유 ID
          station_id (int): 스테이션 번호
          model_type (str): 모델 종류 ("PatchCore" 또는 "YOLO11")
          data_path (str): 학습 데이터 경로
          request_id (str): 원래 요청의 고유 ID

        반환값:
          없음 (None)
        """
        # 현재 학습 중인 작업 ID를 설정한다 (다른 학습 요청을 거부하기 위해).
        self._current_training = task_id
        logger.info("Training started: %s (station=%d, type=%s)", task_id, station_id, model_type)

        # 현재 실행 중인 이벤트 루프를 가져온다.
        # 이벤트 루프: asyncio의 핵심으로, 비동기 작업들을 스케줄링하고 실행한다.
        loop = asyncio.get_running_loop()

        def progress_callback(info: dict) -> None:
            """학습 진행 알림을 운용서버로 전송하는 콜백 함수.

            용도:
              PatchcoreTrainer나 YoloTrainer가 진행률을 보고할 때 호출된다.
              동기 함수(스레드)에서 비동기 함수를 호출하기 위해
              run_coroutine_threadsafe()를 사용한다.

            매개변수:
              info (dict): 진행 정보 (station_id, model_type, progress, status 등)

            반환값:
              없음 (None)
            """
            # run_coroutine_threadsafe(): 다른 스레드에서 비동기 코루틴을 안전하게 실행한다.
            # 학습은 별도 스레드에서 실행되지만, TCP 전송은 메인 이벤트 루프에서 해야 하므로
            # 이 함수를 사용하여 이벤트 루프에 작업을 요청한다.
            asyncio.run_coroutine_threadsafe(
                self._send_progress(info, request_id), loop
            )

        try:
            # model_type에 따라 해당 학습 함수를 실행한다.
            if model_type.upper() == "YOLO11":
                # run_in_executor(None, 함수, 인자들): 동기 함수를 별도 스레드에서 비동기적으로 실행한다.
                # None은 기본 스레드 풀 사용을 의미한다.
                # 이유: 학습은 오래 걸리는 동기 작업이므로, 메인 이벤트 루프를 블로킹하지 않기 위해
                # 별도 스레드에서 실행해야 한다.
                result = await loop.run_in_executor(
                    None,                          # 기본 스레드 풀 사용
                    self._train_yolo,              # 실행할 함수
                    station_id, data_path, progress_callback,  # 함수에 전달할 인자들
                )
            else:
                # PatchCore 학습 실행 (기본값)
                result = await loop.run_in_executor(
                    None,
                    self._train_patchcore,
                    station_id, data_path, progress_callback,
                )

            # 학습 결과에 따라 완료 또는 실패 알림을 운용서버에 전송한다.
            # station_id와 model_type을 result에 주입 → MainServer의 TrainService.validate() 통과 보장
            result["station_id"] = station_id
            result["model_type"] = model_type
            if result["success"]:
                # 학습 성공 -> TRAIN_COMPLETE(1104) 전송
                await self._send_train_complete(result, request_id)
            else:
                # 학습 실패 -> TRAIN_FAIL(1106) 전송
                await self._send_train_fail(result, request_id)

        except Exception as exc:
            # 예상치 못한 에러 발생 시 실패 알림을 전송한다.
            logger.exception("Training task error: %s", exc)
            await self._send_train_fail(
                {"message": str(exc), "version": "", "model_path": "",
                 "station_id": station_id, "model_type": model_type},
                request_id,
            )
        finally:
            # 학습 완료/실패와 관계없이 항상 실행되는 정리 코드이다.
            # 현재 학습 작업 ID를 None으로 초기화하여 다음 학습을 받을 수 있게 한다.
            self._current_training = None
            logger.info("Training finished: %s", task_id)

    # ── 학습 실행 함수 (executor에서 동기적으로 실행됨) ──

    def _train_patchcore(self, station_id: int, data_path: str,
                         progress_callback) -> dict:
        """PatchCore 학습을 실행하는 동기 함수.

        용도:
          run_in_executor()에 의해 별도 스레드에서 호출된다.
          데이터 증강 -> PatchcoreTrainer 학습 순서로 실행한다.

        매개변수:
          station_id (int): 스테이션 번호
          data_path (str): 학습 데이터 경로 (빈 문자열이면 기본 경로 사용)
          progress_callback: 진행률 콜백 함수

        반환값 (dict):
          PatchcoreTrainer.train()의 반환값 (success, model_path, version, accuracy, message)
        """
        # data_path가 비어있으면 기본 데이터 경로를 사용한다.
        # Station1 PatchCore   : ./data/station1/normal      (빈 용기 정상 이미지)
        # Station2 PatchCore   : ./data/station2/patchcore   (라벨 표면 정상 crop) — SETUP_GUIDE 참조
        # (Station2 YOLO 는 _train_yolo 에서 별도 처리)
        subfolder = "patchcore" if station_id == 2 else "normal"
        data_dir = data_path or str(Path(self._config.data_root) / f"station{station_id}" / subfolder)

        # 데이터 증강을 실행한다 (학습 전에 이미지 수를 늘린다).
        try:
            augment_dataset(data_dir, factor=self._config.augmentation_factor)
        except Exception as exc:
            # 증강 실패는 치명적이지 않으므로 경고 로그만 남기고 학습은 계속한다.
            logger.warning("Augmentation skipped: %s", exc)

        # PatchcoreTrainer 객체를 생성하고 학습을 실행한다.
        trainer = PatchcoreTrainer(
            station_id=station_id,                        # 스테이션 번호
            data_dir=data_dir,                            # 학습 이미지 폴더
            output_dir=self._config.model_output_dir,     # 모델 저장 폴더
            backbone=self._config.patchcore_backbone,     # 백본 모델명
            input_size=self._config.patchcore_input_size, # 입력 이미지 크기
            batch_size=self._config.patchcore_batch_size, # 배치 크기
            num_workers=self._config.patchcore_num_workers, # 데이터 로딩 워커 수
            device=self._config.device,                   # GPU/CPU 선택
            progress_callback=progress_callback,          # 진행률 콜백
        )

        # 학습 실행 후 결과를 반환한다.
        return trainer.train()

    def _train_yolo(self, station_id: int, data_path: str,
                    progress_callback) -> dict:
        """YOLO11 학습을 실행하는 동기 함수.

        용도:
          run_in_executor()에 의해 별도 스레드에서 호출된다.
          data.yaml 생성(필요 시) -> YoloTrainer 학습 순서로 실행한다.

        매개변수:
          station_id (int): 스테이션 번호
          data_path (str): 학습 데이터 경로 (빈 문자열이면 기본 경로 사용)
          progress_callback: 진행률 콜백 함수

        반환값 (dict):
          YoloTrainer.train()의 반환값 (success, model_path, version, accuracy, message)
        """
        # data_path가 비어있으면 기본 YOLO 데이터 경로를 사용한다.
        # 예: ./data/station2/yolo
        data_dir = data_path or str(Path(self._config.data_root) / f"station{station_id}" / "yolo")

        # YOLO 학습에 필요한 data.yaml 파일의 경로이다.
        data_yaml = str(Path(data_dir) / "data.yaml")

        # v0.14.7: 기존 data.yaml 이 있어도 **항상 재생성**.
        #   이전엔 user/Roboflow 의 data.yaml 을 그대로 썼는데, 상대경로(images/val 등)가
        #   실제 디스크 레이아웃과 안 맞으면 Ultralytics 가 "images not found" 로 실패.
        #   create_data_yaml 은 실제 폴더 구조(표준/Roboflow/Flat)를 자동 감지해 올바른
        #   yaml 을 덮어씀 → 데이터 재배치 없이 그대로 학습 돌아감.
        data_yaml = create_data_yaml(data_dir, data_yaml)

        # YoloTrainer 객체를 생성하고 학습을 실행한다.
        trainer = YoloTrainer(
            data_yaml=data_yaml,                          # 데이터 설정 파일 경로
            output_dir=self._config.model_output_dir,     # 모델 저장 폴더
            base_model=self._config.yolo_base_model,      # 사전학습 모델 파일명
            input_size=self._config.yolo_input_size,      # 입력 이미지 크기
            epochs=self._config.yolo_epochs,              # 학습 에폭 수
            batch_size=self._config.yolo_batch_size,      # 배치 크기
            patience=self._config.yolo_patience,          # 조기 종료 patience
            device=self._config.device,                   # GPU/CPU 선택
            progress_callback=progress_callback,          # 진행률 콜백
        )

        # 학습 실행 후 결과를 반환한다.
        return trainer.train()

    # ── 운용서버 알림 전송 함수들 ──

    async def _send_progress(self, info: dict, request_id: str) -> None:
        """TRAIN_PROGRESS(1102) 패킷을 운용서버에 전송한다.

        용도:
          학습 진행률을 운용서버에 알려서 UI에 표시할 수 있게 한다.
          예: "PatchCore 학습 40% 진행 중..."

        매개변수:
          info (dict): 진행 정보 딕셔너리 (station_id, model_type, progress, epoch, loss, status)
          request_id (str): 원래 학습 요청의 고유 ID

        반환값:
          없음 (None)
        """
        # 진행 정보를 패킷 본문으로 구성한다.
        body = {
            "station_id": info.get("station_id", 0),  # 스테이션 번호
            "model_type": info.get("model_type", ""),  # 모델 종류 (PatchCore/YOLO11)
            "progress": info.get("progress", 0),       # 진행률 (0~100%)
            "epoch": info.get("epoch", 0),             # 현재 에폭 번호
            "loss": info.get("loss", 0.0),             # 현재 손실값(loss)
            "status": info.get("status", ""),           # 현재 단계 설명 문자열
        }

        # PacketBuilder로 TRAIN_PROGRESS 패킷을 만든다.
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_PROGRESS),  # 프로토콜 번호 1102
            body_dict=body,
            request_id=request_id,
        )

        # 운용서버로 패킷을 전송한다.
        await self._send_to_main(packet)

    async def _send_train_complete(self, result: dict, request_id: str) -> None:
        """TRAIN_COMPLETE(1104) 패킷을 운용서버에 전송한다.

        용도:
          학습이 성공적으로 완료되었음을 운용서버에 알린다.
          모델 파일 경로, 버전, 정확도 등의 정보를 함께 전송한다.

        매개변수:
          result (dict): 학습 결과 딕셔너리 (model_path, version, accuracy, message)
          request_id (str): 원래 학습 요청의 고유 ID

        반환값:
          없음 (None)
        """
        # 학습 완료 정보를 패킷 본문으로 구성한다.
        # MainServer TrainService.validate()가 station_id/model_type 검증하므로 반드시 포함
        model_path = result.get("model_path", "")
        body = {
            "station_id": result.get("station_id", 0),    # 스테이션 ID (1 또는 2)
            "model_type": result.get("model_type", ""),   # 모델 종류 (PatchCore/YOLO11)
            "model_path": model_path,                     # 원본 모델 파일 경로 (학습서버 로컬)
            "version": result.get("version", ""),         # 모델 버전
            "accuracy": result.get("accuracy", 0.0),      # 정확도 (AUROC 또는 mAP50)
            "message": result.get("message", ""),         # 결과 설명 메시지
        }

        # 모델 파일을 바이너리로 읽어서 패킷에 첨부한다.
        # 메인서버(다른 PC)에서 모델 파일을 저장할 수 있도록 TCP로 전송.
        model_bytes: bytes | None = None
        if model_path:
            try:
                with open(model_path, "rb") as f:
                    model_bytes = f.read()
                logger.info("모델 파일 읽기 완료: %s (%d bytes)",
                            model_path, len(model_bytes))
            except Exception as exc:
                logger.warning("모델 파일 읽기 실패: %s — %s", model_path, exc)

        # TRAIN_COMPLETE 패킷을 만들어 전송한다.
        # image_bytes 파라미터에 모델 바이너리를 실어 보낸다.
        # 패킷 구조: [4바이트 헤더] + [JSON(image_size=N)] + [모델 바이너리(N bytes)]
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_COMPLETE),
            body_dict=body,
            request_id=request_id,
            image_bytes=model_bytes,
        )
        await self._send_to_main(packet)

        logger.info("TRAIN_COMPLETE sent: %s (model=%d bytes)",
                    result.get("message"),
                    len(model_bytes) if model_bytes else 0)

    async def _send_train_fail(self, result: dict, request_id: str) -> None:
        """TRAIN_FAIL(1106) 패킷을 운용서버에 전송한다.

        용도:
          학습이 실패했음을 운용서버에 알린다.
          에러 코드와 에러 메시지를 함께 전송한다.

        매개변수:
          result (dict): 실패 결과 딕셔너리 (message, version)
          request_id (str): 원래 학습 요청의 고유 ID

        반환값:
          없음 (None)
        """
        # 학습 실패 정보를 패킷 본문으로 구성한다.
        # GuiNotifier가 클라이언트에 푸시할 때 station_id/model_type이 필요하므로 포함
        body = {
            "station_id": result.get("station_id", 0),
            "model_type": result.get("model_type", ""),
            "error_code": "TRAIN_ERROR",                    # 에러 코드 (고정값)
            "message": result.get("message", "Unknown error"),  # 에러 메시지
            "version": result.get("version", ""),               # 모델 버전 (실패해도 기록)
        }

        # TRAIN_FAIL 패킷을 만들어 전송한다.
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.TRAIN_FAIL),  # 프로토콜 번호 1106
            body_dict=body,
            request_id=request_id,
        )
        await self._send_to_main(packet)

        # 에러 로그를 남긴다.
        logger.error("TRAIN_FAIL sent: %s", result.get("message"))


# ── 진입점 (Entry Point) ──
# 이 파일을 직접 실행했을 때 호출되는 메인 함수이다.

async def main() -> None:
    """프로그램의 메인 비동기 함수.

    용도:
      1) TrainingConfig 설정 객체를 생성한다.
      2) TrainingServer를 생성하고 시작한다.
      3) Ctrl+C(SIGINT) 또는 SIGTERM 시그널을 받으면 안전하게 종료한다.

    매개변수:
      없음

    반환값:
      없음 (None)
    """
    # config/config.json에서 학습 서버 설정 로드
    # 모든 IP/포트/경로/하이퍼파라미터는 프로젝트 루트의 config 파일에서 관리된다.
    config = TrainingConfig.from_json()

    # TrainingServer 인스턴스를 생성한다.
    server = TrainingServer(config)

    # 현재 실행 중인 이벤트 루프를 가져온다.
    loop = asyncio.get_running_loop()

    # 종료 이벤트: set()이 호출되면 wait()에서 대기 중인 코루틴이 깨어난다.
    stop_event = asyncio.Event()

    def request_stop() -> None:
        """시그널 핸들러: 종료 요청을 받으면 stop_event를 설정한다.

        용도:
          Ctrl+C(SIGINT)나 kill 명령(SIGTERM)을 받으면 호출되어
          프로그램의 안전한 종료(graceful shutdown)를 시작한다.
        """
        stop_event.set()  # 이벤트를 설정하여 대기 중인 main()을 깨운다

    # SIGINT(Ctrl+C)와 SIGTERM(kill 명령)에 대한 핸들러를 등록한다.
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            # add_signal_handler(): 해당 시그널을 받으면 request_stop 함수를 호출하도록 등록한다.
            loop.add_signal_handler(sig, request_stop)
        except NotImplementedError:
            # Windows에서는 add_signal_handler가 지원되지 않을 수 있다.
            # 그 경우 무시하고 넘어간다 (Ctrl+C는 KeyboardInterrupt로 처리됨).
            pass

    # 서버를 백그라운드 태스크로 시작한다.
    server_task = loop.create_task(server.run())

    try:
        # 종료 시그널을 받을 때까지 여기서 대기한다.
        await stop_event.wait()
    except asyncio.CancelledError:
        # 태스크가 취소된 경우 (정상 종료 과정의 일부)
        pass

    # 서버 태스크를 취소(cancel)하여 안전하게 종료한다.
    server_task.cancel()

    try:
        # 취소된 태스크가 완전히 종료될 때까지 기다린다.
        await server_task
    except asyncio.CancelledError:
        # 취소 완료 (정상)
        pass


# 이 파일을 직접 실행했을 때만 main()을 호출한다.
# "python -m Training.TrainingMain" 또는 "python TrainingMain.py"로 실행할 때 동작한다.
# 다른 파일에서 import할 때는 실행되지 않는다.
if __name__ == "__main__":
    # asyncio.run(): 비동기 메인 함수를 실행하고, 완료될 때까지 블로킹한다.
    # 내부적으로 이벤트 루프를 생성하고, main() 코루틴을 실행하고, 종료 시 루프를 닫는다.
    asyncio.run(main())
