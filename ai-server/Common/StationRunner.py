"""StationRunner.py
AI 추론 서버의 비동기 큐 파이프라인 베이스.

파이프라인:
  [Pylon Grab Producer] -> grab_queue -> [Inference Worker(s)] -> result_queue -> [Sender Worker(s)] -> 메인 서버
  [OK Stat Reporter] (별도 코루틴) -> 주기적 STATION_OK_COUNT(1004) 송신
  [Inference Worker] -> INSPECT_META(1006) 송신 (OK/NG 공통)

설계 원칙:
- 모든 단계는 asyncio.Queue로 분리 → 단계별 백프레셔/병렬도 독립 조정.
- 추론은 CPU/GPU 바운드이므로 loop.run_in_executor 로 별도 스레드에서 실행.
- NG는 ACK 기반 송신, OK는 카운터만 누적 후 주기 송신.
- inspection_id는 'stationN-YYYYMMDDHHMMSSmmm-seq' 형식으로 추론서버에서 발급.
"""

# ---------------------------------------------------------------------------
# [임포트 영역]
# 파이썬에서 다른 모듈(라이브러리)의 기능을 가져와 이 파일에서 사용하기 위한 부분입니다.
# ---------------------------------------------------------------------------

# __future__에서 annotations를 가져오면 타입 힌트(예: list[int])를 문자열로 지연 평가합니다.
# 파이썬 3.9 이하에서도 최신 타입 힌트 문법(예: list[int] 대신 List[int])을 사용할 수 있게 해줍니다.
from __future__ import annotations

# asyncio: 파이썬의 비동기(async/await) 프로그래밍 라이브러리입니다.
# 여러 작업(카메라 촬영, 추론, 결과 전송)을 동시에 수행할 수 있도록 "이벤트 루프"를 제공합니다.
# 비동기란? → 하나의 작업이 끝나길 기다리지 않고 다른 작업을 먼저 처리하는 방식입니다.
import asyncio

# logging: 프로그램 실행 중 발생하는 이벤트(정보, 경고, 에러)를 기록하는 라이브러리입니다.
# print() 대신 logging을 쓰면 심각도 레벨 구분, 파일 저장 등이 가능합니다.
import logging

# time: 시간 관련 유틸리티입니다. time.time()은 현재 시각(초 단위), time.perf_counter()는
# 고해상도 성능 측정용 타이머를 제공합니다. 추론 소요 시간(latency) 측정에 사용합니다.
import time

# datetime: 날짜/시간 객체를 다루는 라이브러리입니다.
# timezone: UTC 같은 시간대 정보를 표현합니다.
# inspection_id 생성 시 타임스탬프를 만들거나 ISO 형식 시간 문자열을 생성할 때 사용합니다.
from datetime import datetime, timezone

# typing: 타입 힌트(변수가 어떤 타입인지 알려주는 표기)에 필요한 도구들입니다.
# Any: 아무 타입이나 가능, Optional: None이 될 수도 있는 타입을 의미합니다.
from typing import Any, Optional

# StationConfig: 이 스테이션(검사 장비)의 설정값을 담는 클래스입니다.
# 서버 주소, 포트, 모델 경로, 워커 수 등의 설정을 읽어옵니다.
from Common.Config import StationConfig

# BaseInferencer: AI 모델을 로드하고 이미지에 대해 추론(예측)을 수행하는 기본 클래스입니다.
# 실제 딥러닝 모델(예: YOLO, ResNet 등)은 이 클래스를 상속받아 구현합니다.
from Common.Inferencer import BaseInferencer

# PacketBuilder: 메인 서버로 보낼 네트워크 패킷(데이터 묶음)을 조립하는 유틸리티입니다.
# 프로토콜 번호, 본문(body), 이미지 등을 하나의 바이트 패킷으로 만들어줍니다.
from Common.Packet import PacketBuilder

# ProtocolNo: 서버 간 통신에서 "이 패킷이 무슨 종류인지" 구분하는 프로토콜 번호 열거형(enum)입니다.
# 예: 1004=OK카운트, 1006=검사메타 등 번호로 패킷 종류를 구분합니다.
from Common.Protocol import ProtocolNo

# SerialCtrl: 아두이노와 시리얼(USB) 통신을 담당하는 클래스입니다.
# NG(불량) 판정 시 아두이노에 명령을 보내 서보모터, LED, 부저 등을 제어합니다.
from Common.SerialCtrl import SerialCtrl

# TcpClient: 메인 서버와 TCP 소켓 통신을 담당하는 클래스입니다.
# 패킷을 보내고 응답(ACK)을 받거나, 일방적으로 전송(fire-and-forget)하는 기능을 제공합니다.
from Common.TcpClient import TcpClient

# PylonCamera: Basler 산업용 카메라 래퍼. pypylon 미탑재/미연결 시 is_open=False
# 로 떨어지므로 호출자가 더미 이미지로 폴백할 수 있다. (v0.11.0)
from Common.PylonCamera import PylonCamera


# 이 모듈 전용 로거(logger)를 생성합니다.
# __name__은 현재 모듈 이름(예: "Common.StationRunner")이 되어,
# 로그 출력 시 어디서 발생한 로그인지 구분할 수 있습니다.
logger = logging.getLogger(__name__)


# OK 카운트를 메인 서버에 보고하는 주기(초 단위)입니다.
# 5초마다 "지금까지 OK 몇 개, NG 몇 개, 평균 추론 시간" 정보를 한 번에 보냅니다.
# 매번 OK마다 패킷을 보내면 네트워크 부하가 크므로, 일정 주기로 모아서 보내는 것입니다.
OK_COUNT_REPORT_INTERVAL_SEC = 5.0


# ===========================================================================
# _downscale_for_transport — NG 3장 이미지 전송용 다운스케일 헬퍼 (v0.14.6)
# ===========================================================================
# 문제:
#   카메라 원본 1920x1200 으로 만든 히트맵/마스크 PNG 가 각 2~3 MB 에 달해
#   TCP 전송 중 부분 유실이 발생하면 MFC 에서 하단이 검정으로 잘려 보임.
#   (PNG 는 스트림 디코딩 특성상 앞부분부터 복원되기 때문)
#
# 해결:
#   긴 변이 max_side 보다 크면 비율 유지로 축소. 이미지 면적이 크게 줄어
#   PNG 크기도 ~1/3 이하로 떨어지고 전송 안정성이 크게 향상된다.
#   AI 추론용 이미지는 건드리지 않고, "전송용 인코딩 직전" 에만 적용한다.
# ===========================================================================
def _downscale_for_transport(image, max_side: int = 1280):
    """이미지의 긴 변이 max_side 이하가 되도록 비율 유지 다운스케일.

    Args:
        image: BGR ndarray (H, W, 3). None 이면 None 반환.
        max_side: 긴 변의 최대 픽셀 수 (기본 1280).

    Returns:
        ndarray: 축소된 이미지. 원본이 이미 작으면 원본 그대로.
    """
    if image is None:
        return None
    try:
        import cv2 as _cv2
    except ImportError:
        return image  # cv2 없으면 다운스케일 없이 원본 반환 (fail-safe)

    h, w = image.shape[:2]
    long_side = max(h, w)
    if long_side <= max_side:
        return image  # 이미 작음 — 리사이즈 불필요

    scale = max_side / float(long_side)
    new_w = max(1, int(round(w * scale)))
    new_h = max(1, int(round(h * scale)))
    # INTER_AREA: 축소 시 가장 품질 좋은 필터 (cv2 권장)
    return _cv2.resize(image, (new_w, new_h), interpolation=_cv2.INTER_AREA)


# ===========================================================================
# GrabItem 클래스
# ---------------------------------------------------------------------------
# 목적: 카메라에서 촬영한 이미지 한 프레임의 정보를 담는 데이터 컨테이너(그릇)입니다.
# grab_queue에 넣어서 프로듀서(촬영) → 추론 워커로 전달하는 단위가 됩니다.
# ===========================================================================
class GrabItem:
    """카메라 grab 1프레임."""

    # __slots__: 이 클래스가 가질 수 있는 속성(변수)을 미리 고정합니다.
    # 일반 클래스는 내부에 딕셔너리(__dict__)를 사용해 속성을 저장하는데,
    # __slots__를 쓰면 딕셔너리 대신 고정 크기 배열을 사용하므로 메모리를 절약합니다.
    # 수천~수만 개의 GrabItem이 생성될 수 있으므로 메모리 최적화가 중요합니다.
    __slots__ = ("frame_id", "image", "captured_at")

    # -----------------------------------------------------------------------
    # GrabItem 생성자 (__init__)
    # -----------------------------------------------------------------------
    # 목적: GrabItem 객체를 초기화합니다 (새로 만들 때 호출됩니다).
    # 매개변수:
    #   frame_id (int): 프레임 일련번호. 몇 번째 촬영인지 추적하기 위한 고유 번호입니다.
    #   image (Any): 촬영된 이미지 데이터. 보통 numpy ndarray(OpenCV BGR 형식)입니다.
    #                 Any 타입인 이유는 카메라 종류에 따라 형식이 다를 수 있기 때문입니다.
    #   captured_at (float): 촬영 시각. time.time()으로 얻은 유닉스 타임스탬프(초 단위)입니다.
    #                        나중에 "촬영부터 추론 완료까지 걸린 시간"을 계산할 때 사용합니다.
    # 반환값: 없음 (생성자는 객체 자신을 반환합니다)
    # -----------------------------------------------------------------------
    def __init__(self, frame_id: int, image: Any, captured_at: float):
        # 프레임 일련번호를 저장합니다. 디버깅이나 로그에서 어떤 프레임인지 식별할 때 씁니다.
        self.frame_id = frame_id
        # 실제 이미지 데이터(numpy 배열)를 저장합니다. 추론 워커가 이 이미지로 AI 모델을 돌립니다.
        self.image = image
        # 촬영 시각을 저장합니다. 파이프라인 각 단계의 지연 시간을 측정하는 데 활용할 수 있습니다.
        self.captured_at = captured_at


# ===========================================================================
# ResultItem 클래스
# ---------------------------------------------------------------------------
# 목적: AI 추론 결과 1건의 정보를 담는 데이터 컨테이너입니다.
# NG(불량) 판정된 경우에만 result_queue에 넣어서 Sender Worker로 전달됩니다.
# OK(양품)는 카운터만 올리고 별도 패킷을 보내지 않으므로 ResultItem을 만들지 않습니다.
# ===========================================================================
class ResultItem:
    """추론 1건 결과 (NG만 큐에 들어감)."""

    # __slots__로 속성을 고정하여 메모리를 절약합니다.
    # 이미지(image_bytes)가 포함되어 있어 한 객체의 크기가 클 수 있으므로 최적화가 중요합니다.
    # 히트맵/마스크는 NG 시각화용으로 추가된 필드입니다 (MFC 클라이언트 3분할 표시용).
    __slots__ = ("inspection_id", "result_dict", "image_bytes",
                 "heatmap_bytes", "pred_mask_bytes", "latency_ms")

    # -----------------------------------------------------------------------
    # ResultItem 생성자 (__init__)
    # -----------------------------------------------------------------------
    # 목적: ResultItem 객체를 초기화합니다.
    # 매개변수:
    #   inspection_id (str): 검사 고유 ID. "station1-20260416120000123-000001" 같은 형식입니다.
    #                        메인 서버 DB에서 이 검사 기록을 찾을 때 사용하는 키(key)입니다.
    #   result_dict (dict): 추론 결과를 담은 딕셔너리. 예: {"result": "NG", "defect": "scratch"}
    #                       어떤 종류의 불량인지, 신뢰도(confidence)는 얼마인지 등의 정보입니다.
    #   image_bytes (Optional[bytes]): 불량 이미지의 JPEG 인코딩 바이트. 메인 서버에서 불량 이미지를
    #                                  저장/표시할 때 사용합니다. 이미지가 없으면 None입니다.
    #   latency_ms (int): 추론에 걸린 시간(밀리초). 성능 모니터링에 사용합니다.
    # 반환값: 없음
    # -----------------------------------------------------------------------
    def __init__(self, inspection_id: str, result_dict: dict,
                 image_bytes: Optional[bytes], latency_ms: int,
                 heatmap_bytes: Optional[bytes] = None,
                 pred_mask_bytes: Optional[bytes] = None):
        """
        매개변수:
          inspection_id: 검사 고유 ID
          result_dict: 추론 결과 dict (JSON으로 전송됨)
          image_bytes: 원본 이미지 JPEG 바이트 (검사 증거 자료)
          latency_ms: 추론 소요 시간
          heatmap_bytes: 원본+히트맵 합성 PNG 바이트 (MFC 시각화용, 없으면 None)
          pred_mask_bytes: 원본+Pred Mask 합성 PNG 바이트 (MFC 시각화용, 없으면 None)
        """
        # 검사 고유 ID를 저장합니다. 패킷에 포함되어 메인 서버로 전송됩니다.
        self.inspection_id = inspection_id
        # 추론 결과 딕셔너리를 저장합니다. NG 패킷의 본문(body)이 됩니다.
        self.result_dict = result_dict
        # JPEG로 인코딩된 불량 이미지 바이트를 저장합니다. 패킷에 첨부되어 전송됩니다.
        self.image_bytes = image_bytes
        # 원본+히트맵 합성 PNG 바이트. MFC 클라이언트가 "Anomaly Map" 영역에 표시할 이미지.
        self.heatmap_bytes = heatmap_bytes
        # 원본+Pred Mask 합성 PNG 바이트. MFC 클라이언트가 "Pred Mask" 영역에 표시할 이미지.
        self.pred_mask_bytes = pred_mask_bytes
        # 추론 소요 시간(밀리초)을 저장합니다. 서버에서 성능 통계를 낼 때 활용됩니다.
        self.latency_ms = latency_ms


# ===========================================================================
# StationRunner 클래스
# ---------------------------------------------------------------------------
# 목적: 이 파일의 핵심 클래스입니다. 전체 비동기 파이프라인을 구성하고 실행합니다.
#
# 파이프라인 구조:
#   1. Grab Producer: 카메라에서 이미지를 촬영하여 grab_queue에 넣습니다.
#   2. Inference Worker(s): grab_queue에서 이미지를 꺼내 AI 모델로 추론합니다.
#   3. Sender Worker(s): NG 결과를 result_queue에서 꺼내 메인 서버로 전송합니다.
#   4. OK Count Reporter: 주기적으로 OK/NG 통계를 메인 서버에 보고합니다.
#
# 이 구조를 "생산자-소비자 패턴(Producer-Consumer Pattern)"이라고 합니다.
# 각 단계가 독립적으로 동작하므로, 한 단계가 느려도 다른 단계에 영향을 최소화합니다.
# ===========================================================================
class StationRunner:
    """비동기 큐 파이프라인 실행기."""

    # _SENTINEL: 파이프라인 종료를 알리는 특수 신호 객체입니다.
    # 큐에 이 객체가 들어오면 "더 이상 처리할 데이터가 없다, 종료하라"는 의미입니다.
    # object()로 만든 고유 객체이므로 일반 데이터와 절대 혼동되지 않습니다.
    # 이런 패턴을 "포이즌 필(poison pill)" 또는 "센티널(sentinel)" 패턴이라고 합니다.
    _SENTINEL = object()

    # -----------------------------------------------------------------------
    # StationRunner 생성자 (__init__)
    # -----------------------------------------------------------------------
    # 목적: StationRunner 객체를 초기화합니다. 필요한 컴포넌트(TCP 클라이언트, 시리얼 통신,
    #       비동기 큐 등)를 생성하고 초기 상태를 설정합니다.
    # 매개변수:
    #   config (StationConfig): 스테이션 설정 객체. 서버 주소, 포트, 워커 수 등을 담고 있습니다.
    #   inferencer (BaseInferencer): AI 추론기 객체. load_model()과 infer() 메서드를 제공합니다.
    # 반환값: 없음
    # -----------------------------------------------------------------------
    def __init__(self,
                 config: StationConfig,
                 inferencer: BaseInferencer):
        # 설정 객체를 인스턴스 변수로 저장합니다. 나중에 station_id, 워커 수 등을 참조할 때 씁니다.
        self._config = config
        # AI 추론기 객체를 저장합니다. 이미지를 넣으면 OK/NG 판정 결과를 돌려주는 핵심 엔진입니다.
        self._inferencer = inferencer

        # 메인 서버와 통신할 TCP 클라이언트를 생성합니다.
        # config에서 메인 서버의 IP 주소(host)와 포트 번호(port)를 가져와 연결 대상을 지정합니다.
        self._tcp_client = TcpClient(config.main_server_host, config.main_server_port)
        # TCP 클라이언트에 이 스테이션의 ID를 알려줍니다.
        # 패킷 전송 시 "어떤 스테이션에서 보낸 건지" 식별하기 위해 필요합니다.
        self._tcp_client.set_station_id(config.station_id)
        # 메인 서버에서 "모델을 다시 로드해라"라는 명령이 오면 호출할 콜백 함수를 등록합니다.
        # 콜백(callback): 어떤 이벤트가 발생했을 때 자동으로 호출되는 함수를 말합니다.
        self._tcp_client.set_on_model_reload(self._handle_model_reload)

        # v0.14.0: 검사 pause/resume 명령(INFERENCE_CONTROL_CMD 1020) 콜백 등록.
        # 메인서버가 클라이언트 요청을 중계해 오면 grab 이벤트를 on/off 한다.
        self._tcp_client.set_on_inference_control(self._handle_inference_control)

        # 아두이노와 시리얼 통신할 컨트롤러를 생성합니다.
        # NG 판정 시 아두이노에 명령을 보내 물리적 동작(서보모터 리젝트, LED, 부저)을 수행합니다.
        self._serial_ctrl = SerialCtrl(config.arduino_port, config.arduino_baud)

        # Basler Pylon 카메라 핸들 (v0.11.0).
        # config.camera_enabled 가 False 이거나 라이브러리/장치가 없으면 open() 에서
        # False 를 반환해 _run_grab_producer 가 자동으로 더미 이미지 모드로 폴백한다.
        # 실제 배포(.120 PC) 에서는 카메라가 연결되어 있으므로 진짜 프레임을 사용.
        self._camera = PylonCamera(serial=getattr(config, "camera_serial", ""))

        # grab_queue: 카메라 촬영 이미지(GrabItem)를 저장하는 비동기 큐입니다.
        # maxsize를 설정하면 큐가 가득 찼을 때 프로듀서가 대기합니다(백프레셔).
        # 백프레셔(backpressure): 소비자가 처리 못 할 만큼 데이터가 쌓이지 않도록
        #                         생산자의 속도를 자동으로 늦추는 메커니즘입니다.
        self._grab_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)

        # result_queue: NG 추론 결과(ResultItem)를 저장하는 비동기 큐입니다.
        # Inference Worker가 넣고, Sender Worker가 꺼내서 메인 서버로 전송합니다.
        self._result_queue: asyncio.Queue = asyncio.Queue(maxsize=config.grab_queue_max)

        # 실행 중인 비동기 태스크(Task) 목록입니다.
        # 프로그램 종료 시 모든 태스크를 cancel() 하기 위해 추적합니다.
        self._tasks: list[asyncio.Task] = []

        # 파이프라인이 현재 실행 중인지를 나타내는 플래그입니다.
        # False로 바뀌면 각 코루틴의 while 루프가 종료됩니다.
        self._is_running = False

        # v0.14.0: 검사 일시정지 플래그. INFERENCE_CONTROL_CMD(1020) 수신 시 토글됨.
        #   True: grab_producer 가 grab 을 건너뛰고 대기 → 추론/송신 자연스럽게 중단
        #   False (기본): 정상 grab
        # 이벤트 기반으로 즉시 반응 (pause 중 sleep → resume 시 즉시 깨어남).
        self._pause_event: asyncio.Event = asyncio.Event()
        self._pause_event.set()   # 시작 상태 = 실행 (set=진행, clear=일시정지)

        # 프레임 일련번호 카운터입니다. 카메라에서 프레임을 촬영할 때마다 1씩 증가합니다.
        self._frame_seq = 0
        # 검사 ID 발급용 일련번호 카운터입니다. 추론이 완료될 때마다 1씩 증가합니다.
        # inspection_id의 유일성을 보장하기 위해 타임스탬프 + 이 시퀀스 번호를 조합합니다.
        self._inspection_seq = 0  # inspection_id 발급용

        # OK 판정 횟수 카운터입니다. 주기적 보고 후 0으로 초기화(reset)됩니다.
        self._ok_count = 0
        # NG 판정 횟수 카운터입니다. 주기적 보고 후 0으로 초기화됩니다.
        self._ng_count = 0
        # 추론 소요 시간(밀리초)의 합계입니다. 평균 계산용으로, 보고 후 0으로 초기화됩니다.
        self._latency_sum_ms = 0
        # 추론 횟수 카운터입니다. 평균 latency = _latency_sum_ms / _latency_count 로 계산합니다.
        self._latency_count = 0

        # NG 패킷을 보낼 때 사용할 프로토콜 번호를 미리 결정합니다.
        # 스테이션 1번과 2번이 서로 다른 프로토콜 번호를 사용하여,
        # 메인 서버가 "어느 스테이션의 NG인지" 패킷 종류만으로 구분할 수 있습니다.
        if config.station_id == 1:
            # 스테이션 1은 STATION1_NG 프로토콜 번호를 사용합니다.
            self._ng_protocol_no = int(ProtocolNo.STATION1_NG)
        else:
            # 스테이션 2는 STATION2_NG 프로토콜 번호를 사용합니다.
            self._ng_protocol_no = int(ProtocolNo.STATION2_NG)

    # =====================================================================
    # 외부 진입점 (External Entry Points)
    # =====================================================================

    # -----------------------------------------------------------------------
    # run() 메서드
    # -----------------------------------------------------------------------
    # 목적: 파이프라인 전체를 시작하는 메인 진입점입니다.
    #       모델 로드, 시리얼 포트 열기, 모든 워커 코루틴 생성 및 실행을 담당합니다.
    # 매개변수: 없음 (self만 사용)
    # 반환값: None (비동기 함수이므로 코루틴 객체를 반환하지만, 실질 반환값은 없습니다)
    # -----------------------------------------------------------------------
    async def run(self) -> None:
        # 파이프라인 실행 상태를 True로 설정합니다.
        # 각 워커의 while 루프가 이 값을 확인하여 계속 돌지 멈출지 결정합니다.
        self._is_running = True

        # AI 모델을 메모리에 로드합니다. 모델 파일(.pt, .onnx 등)을 읽어서
        # GPU/CPU에 올리는 무거운 작업이므로 파이프라인 시작 전에 미리 수행합니다.
        self._inferencer.load_model()

        # 아두이노와의 시리얼 포트를 엽니다. 이후 NG 판정 시 명령을 보낼 수 있게 됩니다.
        self._serial_ctrl.open()

        # Basler Pylon 카메라를 엽니다 (v0.11.0).
        #   - config.camera_enabled=False 또는 pypylon 미탑재 → 더미 모드
        #   - 장치는 있지만 open 실패(타 프로세스 점유 등) → 더미 모드 폴백
        # 실패해도 예외는 던지지 않고 _camera.is_open 플래그로만 전달 →
        # _run_grab_producer 가 매 프레임 이 플래그를 확인해 실/더미 분기.
        if getattr(self._config, "camera_enabled", True):
            if self._camera.open():
                logger.info("Pylon 카메라 사용 — 실제 프레임 grab 시작")
            else:
                logger.warning("Pylon 카메라 open 실패 — 더미 이미지 모드로 동작")
        else:
            logger.info("config.camera_enabled=false — 더미 이미지 모드")

        # 현재 실행 중인 asyncio 이벤트 루프를 가져옵니다.
        # 이벤트 루프: 비동기 작업들을 스케줄링하고 실행하는 핵심 엔진입니다.
        # create_task()로 코루틴을 이벤트 루프에 등록할 때 필요합니다.
        loop = asyncio.get_running_loop()

        # [1] Grab Producer 태스크를 생성하여 이벤트 루프에 등록합니다.
        # 카메라에서 이미지를 촬영하여 grab_queue에 넣는 역할을 합니다.
        # 1개만 실행합니다 (카메라가 1대이므로).
        self._tasks.append(loop.create_task(self._run_grab_producer()))

        # [2] Inference Worker 태스크를 설정된 개수만큼 생성합니다.
        # 여러 워커를 만들면 추론을 병렬로 수행하여 처리량(throughput)을 높일 수 있습니다.
        # range(N)은 0부터 N-1까지 반복하므로, 워커에 0, 1, 2, ... 번호가 부여됩니다.
        for i in range(self._config.inference_workers):
            self._tasks.append(loop.create_task(self._run_inference_worker(i)))

        # [3] Sender Worker 태스크를 설정된 개수만큼 생성합니다.
        # NG 결과를 메인 서버로 TCP 전송하는 역할입니다.
        # ACK 대기 시간이 있으므로 여러 워커를 두면 전송 처리량이 높아집니다.
        for i in range(self._config.sender_workers):
            self._tasks.append(loop.create_task(self._run_sender_worker(i)))

        # [4] OK Count Reporter 태스크를 생성합니다.
        # 주기적(5초)으로 OK/NG 통계를 메인 서버에 보고하는 역할입니다.
        self._tasks.append(loop.create_task(self._run_ok_count_reporter()))

        # 모든 태스크가 완료될 때까지 대기합니다.
        # asyncio.gather()는 여러 코루틴을 동시에 실행하고, 모두 끝나면 반환합니다.
        # 하나라도 예외가 발생하면 나머지도 취소됩니다.
        try:
            await asyncio.gather(*self._tasks)
        # CancelledError: 태스크가 외부에서 cancel() 되었을 때 발생하는 예외입니다.
        # stop() 호출 시 모든 태스크를 cancel 하므로, 이 예외가 정상적인 종료 경로입니다.
        except asyncio.CancelledError:
            logger.info("StationRunner cancelled")
        # finally: 정상 종료든 에러든 상관없이 반드시 실행되는 블록입니다.
        # 리소스(TCP 연결, 시리얼 포트)를 정리(cleanup)합니다.
        finally:
            await self._teardown()

    # -----------------------------------------------------------------------
    # stop() 메서드
    # -----------------------------------------------------------------------
    # 목적: 파이프라인을 안전하게 종료합니다.
    #       각 큐에 SENTINEL(종료 신호)을 넣고, 모든 태스크를 취소합니다.
    # 매개변수: 없음
    # 반환값: None
    # -----------------------------------------------------------------------
    async def stop(self) -> None:
        # 실행 플래그를 False로 설정하여 while 루프들이 자연스럽게 종료되도록 합니다.
        self._is_running = False
        # grab_queue에 SENTINEL을 넣어 Inference Worker들에게 "더 이상 데이터 없음"을 알립니다.
        await self._grab_queue.put(self._SENTINEL)
        # result_queue에 SENTINEL을 넣어 Sender Worker들에게 "더 이상 결과 없음"을 알립니다.
        await self._result_queue.put(self._SENTINEL)
        # 모든 비동기 태스크를 순회하며 cancel()을 호출합니다.
        # cancel()은 해당 태스크에 CancelledError 예외를 발생시켜 코루틴을 중단합니다.
        for task in self._tasks:
            task.cancel()

    # =====================================================================
    # Producer (생산자)
    # =====================================================================

    # -----------------------------------------------------------------------
    # _run_grab_producer() 메서드 — 카메라 프레임 생산자 (v0.11.0)
    # -----------------------------------------------------------------------
    # 동작:
    #   self._camera.is_open == True  → Basler Pylon 에서 실제 프레임 grab
    #   self._camera.is_open == False → 더미 이미지(랜덤 ndarray) 사용 — 개발/CI 용
    #
    # 프레임 레이트:
    #   config.camera_fps (기본 2.0 fps) 로 sleep 주기 결정.
    #   Pylon 의 GrabStrategy_LatestImageOnly 조합으로 지연 누적 방지.
    #
    # 블로킹 주의:
    #   PylonCamera.grab() 은 동기 블로킹 함수이므로 이벤트 루프를 막지 않도록
    #   loop.run_in_executor 로 스레드 풀에 위임한다. 그 사이 다른 코루틴
    #   (sender/reporter/추론) 은 정상 동작.
    # -----------------------------------------------------------------------
    async def _run_grab_producer(self) -> None:
        loop = asyncio.get_running_loop()
        import numpy as _np

        # config 의 camera_fps 를 sleep 주기(초) 로 변환. 0 이하면 최저 보장값 사용.
        fps = max(0.1, float(getattr(self._config, "camera_fps", 2.0)))
        period = 1.0 / fps

        # v0.14.2: 카메라 미연결 시 사용할 "정상 placeholder" 이미지를 1회만 로드.
        # 기존에는 랜덤 픽셀(100~200)을 매번 생성했는데, 이 경우 PatchCore 가
        # 전 영역을 이상으로 판정 → NG 판정된 히트맵/마스크 PNG 가 압축 효율 최악이라
        # 2~3 MB 급 대용량 패킷이 초당 여러 건 쏟아져 클라이언트 TCP 파싱 경계가 깨지고
        # MFC CImage assertion(atlimage.h:1629) 발생. 이를 원천 차단한다.
        #
        # 해결: 실제 학습 분포(정상 이미지)와 유사한 고정 이미지를 반복 사용하면
        # PatchCore 가 OK 로 판정 → NG 푸시 자체가 발생하지 않음.
        placeholder: Optional[_np.ndarray] = None

        # v0.14.10: 카메라 미연결 상태에서 config.test_image_dir 가 지정되어 있으면
        # 해당 폴더의 이미지들을 카메라 대신 순환 재생 (MFC 실시간 테스트용).
        # - 폴더 비어있거나 경로 잘못되면 placeholder 로 폴백.
        # - 각 이미지가 한 번씩 카메라 프레임처럼 파이프라인에 들어감.
        test_image_paths: list = []
        test_image_idx = 0

        if not self._camera.is_open:
            test_dir = str(getattr(self._config, "test_image_dir", "") or "").strip()
            if test_dir:
                test_image_paths = self._collect_test_images(test_dir)
                if test_image_paths:
                    logger.info("[테스트 모드] 이미지 폴더 순환 재생 — %s (%d장)",
                                test_dir, len(test_image_paths))
                else:
                    logger.warning("[테스트 모드] test_image_dir 지정됐으나 이미지 없음: %s",
                                   test_dir)
            # 폴더 비어있거나 미지정 → 기존 placeholder 1장 반복
            if not test_image_paths:
                placeholder = self._load_placeholder_frame()

        try:
            while self._is_running:
                # v0.14.0: pause 상태면 wait — resume 시 즉시 깨어남.
                # _pause_event.is_set() == True 일 때만 진행.
                if not self._pause_event.is_set():
                    logger.info("검사 일시정지 상태 — grab 대기")
                    await self._pause_event.wait()
                    logger.info("검사 재개 — grab 루프 복귀")

                frame: Optional[_np.ndarray] = None

                if self._camera.is_open:
                    # 실제 카메라에서 1프레임 — 블로킹이므로 executor 위임
                    frame = await loop.run_in_executor(None, self._camera.grab, 1000)
                    if frame is None:
                        # 일시적 grab 실패는 로그만 남기고 다음 주기에 재시도
                        logger.warning("카메라 grab 실패 — 이번 프레임 건너뜀")
                        await asyncio.sleep(period)
                        continue
                else:
                    # 카메라 미연결/미지원 — 이미지 공급 전략 (우선순위):
                    #   1) test_image_dir 지정됨 → 폴더 이미지 순환 재생 (v0.14.10)
                    #   2) placeholder 이미지 1장 반복 (v0.14.2)
                    #   3) 회색 단색 (fallback)
                    if test_image_paths:
                        frame = self._load_test_image(test_image_paths[test_image_idx])
                        test_image_idx = (test_image_idx + 1) % len(test_image_paths)
                        if frame is None:
                            # 파일 손상/디코드 실패 시 다음 프레임으로 skip
                            await asyncio.sleep(period)
                            continue
                    elif placeholder is not None:
                        frame = placeholder.copy()  # copy 로 안전하게 per-frame 인스턴스
                    else:
                        frame = _np.full((224, 224, 3), 128, dtype=_np.uint8)

                self._frame_seq += 1
                item = GrabItem(self._frame_seq, frame, time.time())

                # 큐가 가득 차면(maxsize) 공간이 생길 때까지 대기(백프레셔).
                await self._grab_queue.put(item)

                # 주기 조정 — Pylon 은 자체 trigger 속도가 빠르지만 추론 처리량에 맞춤
                await asyncio.sleep(period)

        except asyncio.CancelledError:
            pass

    # =====================================================================
    # Inference Worker (추론 작업자)
    # =====================================================================

    # -----------------------------------------------------------------------
    # _run_inference_worker() 메서드
    # -----------------------------------------------------------------------
    # 목적: grab_queue에서 이미지를 꺼내 AI 모델로 추론(예측)하는 워커 코루틴입니다.
    #       파이프라인의 두 번째 단계이자, 가장 핵심적인 AI 추론을 수행합니다.
    #       추론 결과가 NG이면 result_queue에 넣고, OK이면 카운터만 올립니다.
    # 매개변수:
    #   worker_index (int): 워커 식별 번호. 로그에서 어떤 워커가 에러를 냈는지 구분합니다.
    # 반환값: None
    # -----------------------------------------------------------------------
    async def _run_inference_worker(self, worker_index: int) -> None:
        # 현재 이벤트 루프를 가져옵니다. run_in_executor() 호출에 필요합니다.
        loop = asyncio.get_running_loop()

        # 무한 루프: grab_queue에서 아이템을 계속 꺼내 처리합니다.
        # SENTINEL이 오거나 예외가 발생할 때까지 반복합니다.
        while True:
            # grab_queue에서 아이템을 꺼냅니다.
            # 큐가 비어있으면 새 아이템이 들어올 때까지 여기서 대기합니다(비동기 대기).
            item = await self._grab_queue.get()

            # SENTINEL(종료 신호)인지 확인합니다.
            # "is" 연산자는 객체의 동일성(같은 메모리 주소인지)을 비교합니다.
            if item is self._SENTINEL:
                # SENTINEL을 다시 큐에 넣습니다.
                # 이유: 다른 Inference Worker들도 이 SENTINEL을 받아야 종료할 수 있기 때문입니다.
                # 워커가 3개면 SENTINEL 1개로는 1개 워커만 종료되므로, 다시 넣어서 전파합니다.
                await self._grab_queue.put(self._SENTINEL)
                # 이 워커의 루프를 종료합니다.
                break

            # try 블록: 추론 중 에러가 나도 워커 전체가 죽지 않도록 보호합니다.
            try:
                # 추론 시작 시각을 기록합니다.
                # perf_counter()는 time.time()보다 정밀도가 높은 성능 측정용 타이머입니다.
                # 나노초 단위까지 정확하여 밀리초 단위 latency 측정에 적합합니다.
                t0 = time.perf_counter()

                # AI 모델로 이미지 추론을 실행합니다.
                # run_in_executor(None, func, arg): 동기(blocking) 함수를 별도 스레드에서 실행합니다.
                # - 첫 번째 인자 None: 기본 스레드풀(ThreadPoolExecutor)을 사용합니다.
                # - self._inferencer.infer: 실제 딥러닝 추론 함수 (CPU/GPU 바운드 작업).
                # - item.image: 추론할 이미지 데이터.
                #
                # 왜 run_in_executor를 쓰는가?
                # → AI 추론은 CPU/GPU를 오래 점유하는 작업입니다.
                #   asyncio 이벤트 루프는 단일 스레드이므로, 이런 무거운 작업을 직접 실행하면
                #   이벤트 루프가 블록되어 다른 코루틴(네트워크 송신, 카메라 촬영)이 멈춥니다.
                #   별도 스레드에서 실행하면 이벤트 루프는 계속 다른 작업을 처리할 수 있습니다.
                result_dict = await loop.run_in_executor(
                    None, self._inferencer.infer, item.image
                )

                # 추론 소요 시간을 밀리초(ms) 단위로 계산합니다.
                # perf_counter()의 차이(초 단위)에 1000을 곱하면 밀리초가 됩니다.
                # int()로 소수점 이하를 버립니다.
                latency_ms = int((time.perf_counter() - t0) * 1000)

                # 이 추론 건에 대한 고유 inspection_id를 발급합니다.
                # 형식: "station1-20260416120000123-000001"
                inspection_id = self._issue_inspection_id()

                # 현재 시각을 ISO 8601 형식 문자열로 생성합니다.
                # 예: "2026-04-16T12:00:00.123+00:00"
                # 메인 서버 DB에 검사 시각을 기록할 때 사용합니다.
                timestamp = self._make_iso_timestamp()

                # --- 통계 누적 ---
                # 추론 소요 시간을 합계에 더합니다. 나중에 평균을 계산할 때 사용합니다.
                self._latency_sum_ms += latency_ms
                # 추론 횟수를 1 증가시킵니다. 평균 latency 계산의 분모가 됩니다.
                self._latency_count += 1

                # 추론 결과가 NG(불량)인지 확인합니다.
                # result_dict.get("result")로 안전하게 값을 가져옵니다.
                # .get()은 키가 없어도 에러 대신 None을 반환합니다.
                is_ng = result_dict.get("result") == "NG"

                # NG이면 NG 카운터 증가, 아니면 OK 카운터 증가
                if is_ng:
                    self._ng_count += 1
                else:
                    self._ok_count += 1

                # 프로토콜 1006번 INSPECT_META 패킷을 메인 서버에 전송합니다.
                # OK든 NG든 모든 검사 건에 대해 전송하며, 메인 서버 DB의 inspections 테이블에
                # 기록을 남기기 위한 것입니다. 어떤 시각에 어떤 결과가 나왔는지 추적합니다.
                await self._send_inspect_meta(
                    inspection_id=inspection_id,
                    timestamp=timestamp,
                    latency_ms=latency_ms,
                    result="ng" if is_ng else "ok",
                )

                # NG인 경우에만 추가 처리를 합니다.
                # OK는 카운터만 올리고 끝이지만, NG는 상세 정보를 메인 서버에 보내야 합니다.
                if is_ng:
                    # result_dict에 추가 정보(스테이션ID, 타임스탬프, 지연시간)를 보강합니다.
                    # 메인 서버에서 NG 상세 내용을 저장/표시할 때 필요한 정보입니다.
                    result_dict["station_id"] = self._config.station_id
                    result_dict["timestamp"]  = timestamp
                    result_dict["latency_ms"] = latency_ms

                    # ── 시각화 이미지 3장 생성 (MFC 클라이언트 3분할 표시용) ──
                    # 1. 원본 이미지 (JPEG)         — Station1/2 탭의 "Image" 영역용
                    # 2. 원본+히트맵 합성 (PNG)     — "Image + Anomaly Map" 영역용
                    # 3. 원본+Pred Mask 합성 (PNG) — "Image + Pred Mask" 영역용
                    #
                    # numpy 배열 필드는 JSON 직렬화 전에 꺼내 두고, 시각화에 활용한다.
                    raw_anomaly_map = result_dict.get("raw_anomaly_map")
                    pred_mask_arr   = result_dict.get("pred_mask")

                    # v0.14.6: NG 3장 이미지를 동일한 작은 해상도로 다운스케일해서 전송.
                    # 카메라 원본(1920x1200) 해상도를 그대로 쓰면 히트맵/마스크 PNG가
                    # 2~3MB 로 커져 TCP 스트림에서 부분 유실이 발생하고 클라이언트에서
                    # 이미지가 하단부터 잘려 보이는 현상 발생(MFC 디코드 실패).
                    # 긴 변을 최대 1280px 로 제한하여 PNG 를 작게 유지 + 세 이미지
                    # 모두 동일 해상도로 통일 → MFC 3분할 뷰의 Aspect 도 자동 일치.
                    MAX_SIDE = 1280

                    # 원본을 먼저 다운스케일한 뒤 그 축소본을 모든 시각화 입력으로 사용.
                    # (원본을 줄이면 히트맵/마스크 오버레이도 자동으로 줄어든 크기로 합성됨)
                    img_small = _downscale_for_transport(item.image, MAX_SIDE) \
                                if item.image is not None else None

                    image_bytes = self._encode_image(img_small) \
                                  if img_small is not None else None
                    heatmap_bytes = None
                    pred_mask_bytes = None

                    # 시각화는 원본 이미지가 있을 때만 의미 있음
                    if img_small is not None:
                        try:
                            from Common.Visualizer import (
                                make_heatmap_overlay,
                                make_pred_mask_overlay,
                                encode_image,
                            )
                            # 히트맵 합성: 축소된 원본 + anomaly_map 오버레이
                            if raw_anomaly_map is not None:
                                heatmap_img = make_heatmap_overlay(
                                    img_small, raw_anomaly_map, alpha=0.5
                                )
                                heatmap_bytes = encode_image(heatmap_img, ".png")
                            # 마스크 합성: 축소된 원본 + pred_mask 빨간 윤곽선
                            if pred_mask_arr is not None:
                                mask_img = make_pred_mask_overlay(
                                    img_small, pred_mask_arr
                                )
                                pred_mask_bytes = encode_image(mask_img, ".png")

                            # v0.14.10: Station2(YOLO) 의 경우 raw_anomaly_map/pred_mask 가
                            # 없으니 bbox_overlay(YOLO 탐지 박스 그려진 이미지)를
                            # MFC 3분할 뷰의 "Anomaly Map" 슬롯으로 재활용한다.
                            # → 검사원이 "어떤 요소가 NG 판정됐는지" 즉시 확인 가능.
                            if heatmap_bytes is None:
                                bbox_overlay = result_dict.get("bbox_overlay")
                                if bbox_overlay is not None:
                                    import cv2 as _cv2
                                    bbox_small = _downscale_for_transport(bbox_overlay, MAX_SIDE)
                                    heatmap_bytes = encode_image(bbox_small, ".png")
                        except Exception as exc:
                            logger.warning("시각화 생성 실패: %s", exc)

                    # JSON으로 직렬화할 수 없는 필드(numpy 배열 등)를 제거합니다.
                    # heatmap, bbox_overlay, raw_anomaly_map, pred_mask는 이미지/배열이므로
                    # JSON 본문에는 포함시키지 않습니다 (이미지는 바이너리로 별도 전송).
                    for _key in ("heatmap", "bbox_overlay", "raw_anomaly_map", "pred_mask"):
                        result_dict.pop(_key, None)

                    # ResultItem을 만들어 result_queue에 넣습니다.
                    # Sender Worker가 이 큐에서 꺼내서 메인 서버로 TCP 전송합니다.
                    # 시각화 이미지 2장(히트맵, 마스크)도 함께 전달합니다.
                    await self._result_queue.put(
                        ResultItem(
                            inspection_id, result_dict, image_bytes, latency_ms,
                            heatmap_bytes=heatmap_bytes,
                            pred_mask_bytes=pred_mask_bytes,
                        )
                    )

                    # NG 시 아두이노에 물리적 동작 명령을 보냅니다.
                    # Station1: 서보모터로 불량품을 리젝트(제거)하고 빨간 LED + 부저를 울립니다.
                    # Station2: RGB LED 색상 변경 + LCD에 불량 유형을 표시합니다.
                    self._handle_arduino_action(result_dict)

            # 추론 중 예외가 발생해도 워커는 죽지 않고 다음 아이템을 계속 처리합니다.
            # logger.exception()은 에러 메시지와 함께 스택 트레이스(에러 위치)도 기록합니다.
            except Exception as exc:
                logger.exception("inference worker %d error: %s", worker_index, exc)

    # =====================================================================
    # Sender Worker (NG 전송 작업자, ACK 기반)
    # =====================================================================

    # -----------------------------------------------------------------------
    # _run_sender_worker() 메서드
    # -----------------------------------------------------------------------
    # 목적: result_queue에서 NG 결과를 꺼내 메인 서버로 TCP 전송하는 워커 코루틴입니다.
    #       ACK(수신 확인) 기반으로 전송하므로, 메인 서버가 확실히 받았는지 확인합니다.
    #       ACK가 오지 않으면 재전송을 시도하고, 최종 실패 시 로그를 남깁니다.
    # 매개변수:
    #   worker_index (int): 워커 식별 번호. 로그에서 어떤 워커인지 구분합니다.
    # 반환값: None
    # -----------------------------------------------------------------------
    async def _run_sender_worker(self, worker_index: int) -> None:
        # 무한 루프: result_queue에서 아이템을 계속 꺼내 전송합니다.
        while True:
            # result_queue에서 ResultItem을 꺼냅니다.
            # 큐가 비어있으면 새 아이템이 들어올 때까지 비동기 대기합니다.
            item = await self._result_queue.get()

            # SENTINEL(종료 신호)인지 확인합니다.
            if item is self._SENTINEL:
                # 다른 Sender Worker들도 종료할 수 있도록 SENTINEL을 다시 큐에 넣습니다.
                await self._result_queue.put(self._SENTINEL)
                # 이 워커의 루프를 종료합니다.
                break

            # try 블록: 전송 에러가 나도 워커 전체가 죽지 않도록 보호합니다.
            try:
                # PacketBuilder로 네트워크 패킷을 조립합니다.
                # protocol_no: 이 패킷이 어떤 종류인지 나타냅니다 (STATION1_NG 또는 STATION2_NG).
                # body_dict: NG 결과 정보가 담긴 딕셔너리 (JSON으로 직렬화됩니다).
                # inspection_id: 이 검사 건의 고유 ID.
                # image_bytes: 불량 이미지의 JPEG 바이트 데이터.
                packet = PacketBuilder.build_packet(
                    protocol_no=self._ng_protocol_no,
                    body_dict=item.result_dict,
                    inspection_id=item.inspection_id,
                    image_bytes=item.image_bytes,
                    heatmap_bytes=item.heatmap_bytes,       # 원본+히트맵 (MFC 3분할 표시용)
                    pred_mask_bytes=item.pred_mask_bytes,   # 원본+Pred Mask
                )

                # ACK 기반으로 패킷을 전송합니다.
                # send_with_ack()는 패킷을 보낸 후 메인 서버의 ACK(수신 확인)를 기다립니다.
                # ACK가 오면 True, 타임아웃/실패 시 재시도 후 최종적으로 False를 반환합니다.
                # ACK를 쓰는 이유: NG(불량)은 중요한 데이터이므로 반드시 전달되어야 합니다.
                #                  OK는 누락돼도 카운트가 약간 줄 뿐이지만, NG 누락은 불량 미검출입니다.
                ok = await self._tcp_client.send_with_ack(
                    packet,
                    protocol_no=self._ng_protocol_no,
                    inspection_id=item.inspection_id,
                )

                # ACK를 끝내 받지 못하면(재시도 횟수 초과) 에러 로그를 남깁니다.
                # 이 NG 데이터는 유실됩니다. 실제 운영에서는 로컬 저장소에 백업하는 로직을 추가할 수 있습니다.
                if not ok:
                    logger.error("sender %d: NG send giveup inspection_id=%s",
                                 worker_index, item.inspection_id)

            # 전송 중 예외가 발생해도 워커는 죽지 않고 다음 아이템을 계속 처리합니다.
            except Exception as exc:
                logger.exception("sender worker %d error: %s", worker_index, exc)

    # =====================================================================
    # OK 카운트 주기 송신 (프로토콜 1004번)
    # =====================================================================

    # -----------------------------------------------------------------------
    # _run_ok_count_reporter() 메서드
    # -----------------------------------------------------------------------
    # 목적: 일정 주기(5초)마다 OK/NG 카운트와 평균 추론 시간을 메인 서버에 보고하는 코루틴입니다.
    #       매번 OK마다 패킷을 보내면 네트워크 부하가 크므로, 모아서 한 번에 보냅니다.
    #       메인 서버 대시보드에서 실시간 생산 현황을 표시하는 데 사용됩니다.
    # 매개변수: 없음
    # 반환값: None
    # -----------------------------------------------------------------------
    async def _run_ok_count_reporter(self) -> None:
        # CancelledError를 잡아서 조용히 종료합니다.
        try:
            # _is_running이 True인 동안 계속 반복합니다.
            while self._is_running:
                # 설정된 주기(5초) 동안 대기합니다.
                # 비동기 sleep이므로 대기 중에도 다른 코루틴은 정상 동작합니다.
                await asyncio.sleep(OK_COUNT_REPORT_INTERVAL_SEC)

                # 이 주기 동안 추론이 한 건도 없었으면 보고를 건너뜁니다.
                # 의미 없는 빈 보고를 보내지 않기 위함입니다.
                # 또한 _latency_count가 0이면 아래 나눗셈에서 ZeroDivisionError가 발생합니다.
                if self._latency_count == 0:
                    continue

                # 평균 추론 시간(밀리초)을 계산합니다.
                # 예: 총 합계 500ms, 횟수 10이면 평균 50ms.
                latency_avg = self._latency_sum_ms / self._latency_count

                # 메인 서버에 보낼 보고 데이터를 딕셔너리로 구성합니다.
                body = {
                    # 어떤 스테이션의 보고인지 식별합니다.
                    "station_id":  self._config.station_id,
                    # 이 주기 동안의 OK 판정 횟수입니다.
                    "ok_count":    self._ok_count,
                    # 이 주기 동안의 NG 판정 횟수입니다.
                    "ng_count":    self._ng_count,
                    # 이 주기 동안의 평균 추론 시간(밀리초). 소수점 둘째자리까지 반올림합니다.
                    "latency_avg": round(latency_avg, 2),
                    # 이 통계가 몇 초 동안의 집계인지 표시합니다. 메인 서버가 해석할 때 참고합니다.
                    "period":      f"{int(OK_COUNT_REPORT_INTERVAL_SEC)}s",
                }

                # 통계 카운터를 모두 0으로 초기화(reset)합니다.
                # 다음 주기에는 새로운 통계를 처음부터 집계하기 위함입니다.
                # 주의: 단일 코루틴(이 함수만)이 카운터를 리셋하므로 경쟁 조건(race condition)이 없습니다.
                # 만약 여러 코루틴이 동시에 리셋한다면 스냅샷을 먼저 떠야 안전합니다.
                self._ok_count = 0
                self._ng_count = 0
                self._latency_sum_ms = 0
                self._latency_count = 0

                # 보고 데이터를 네트워크 패킷으로 조립합니다.
                # STATION_OK_COUNT(1004번) 프로토콜을 사용합니다.
                packet = PacketBuilder.build_packet(
                    protocol_no=int(ProtocolNo.STATION_OK_COUNT),
                    body_dict=body,
                )

                # fire-and-forget(보내고 잊기) 방식으로 전송합니다.
                # ACK를 기다리지 않습니다.
                # 이유: OK 카운트 보고는 한두 번 유실돼도 큰 문제가 없습니다.
                # 다음 주기에 또 보내므로, ACK 대기로 인한 지연보다 빠른 전송이 더 중요합니다.
                await self._tcp_client.send_fire_and_forget(packet)

        # stop()에서 task.cancel()이 호출되면 CancelledError가 발생합니다.
        # pass로 무시하여 코루틴이 조용히 종료됩니다.
        except asyncio.CancelledError:
            pass

    # =====================================================================
    # INSPECT_META (프로토콜 1006번) - OK/NG 공통 전송
    # =====================================================================

    # -----------------------------------------------------------------------
    # _send_inspect_meta() 메서드
    # -----------------------------------------------------------------------
    # 목적: 추론 결과(OK든 NG든)를 메인 서버의 inspections 테이블에 기록하기 위해
    #       INSPECT_META(1006번) 패킷을 전송합니다.
    #       이 데이터는 전체 검사 이력(history) 추적에 사용됩니다.
    # 매개변수:
    #   inspection_id (str): 검사 고유 ID. DB 레코드의 기본 키(PK)가 됩니다.
    #   timestamp (str): 검사 시각의 ISO 8601 문자열. DB에 기록됩니다.
    #   latency_ms (int): 추론 소요 시간(밀리초). 성능 분석에 사용됩니다.
    #   result (str): 판정 결과 문자열. "ok" 또는 "ng".
    # 반환값: None
    # -----------------------------------------------------------------------
    async def _send_inspect_meta(self, inspection_id: str, timestamp: str,
                                 latency_ms: int, result: str) -> None:
        # 전송할 데이터를 딕셔너리로 구성합니다.
        body = {
            # 어떤 스테이션의 검사인지 식별합니다.
            "station_id": self._config.station_id,
            # 검사가 수행된 시각입니다. DB에 기록되어 나중에 시간대별 통계를 낼 때 사용됩니다.
            "timestamp":  timestamp,
            # 추론에 걸린 시간(밀리초)입니다. 모델 성능 모니터링에 활용됩니다.
            "latency_ms": latency_ms,
            # 사용된 AI 모델의 ID입니다. (v0.15.0: Inferencer.active_model_id 에서 직접 읽음)
            #   MODEL_RELOAD_CMD 에 "model_db_id" 가 포함되면 실제 값,
            #   포함되지 않으면 0 (= 추적 불가, 이전 호환).
            "model_id":   int(getattr(self._inferencer, "active_model_id", 0)),
            # 판정 결과 ("ok" 또는 "ng").
            "result":     result,
        }

        # INSPECT_META(1006번) 프로토콜로 패킷을 조립합니다.
        # inspection_id를 패킷에 포함시켜 메인 서버가 어떤 검사 건인지 식별할 수 있게 합니다.
        packet = PacketBuilder.build_packet(
            protocol_no=int(ProtocolNo.INSPECT_META),
            body_dict=body,
            inspection_id=inspection_id,
        )

        # fire-and-forget으로 전송합니다.
        # 이 메타 정보도 OK 카운트처럼 ACK 없이 전송합니다.
        # 모든 검사 건마다 ACK를 기다리면 추론 파이프라인이 느려질 수 있기 때문입니다.
        await self._tcp_client.send_fire_and_forget(packet)

    # =====================================================================
    # Helpers (보조 메서드들)
    # =====================================================================

    # -----------------------------------------------------------------------
    # _issue_inspection_id() 메서드
    # -----------------------------------------------------------------------
    # 목적: 검사 건마다 고유한 inspection_id를 발급합니다.
    #       형식: "stationN-YYYYMMDDHHMMSSmmm-NNNNNN"
    #       예시: "station1-20260416120000123-000001"
    #       타임스탬프 + 시퀀스 번호의 조합으로 유일성을 보장합니다.
    # 매개변수: 없음
    # 반환값: str - 발급된 inspection_id 문자열
    # -----------------------------------------------------------------------
    def _issue_inspection_id(self) -> str:
        """stationN-YYYYMMDDHHMMSSmmm-seq 형식으로 발급."""
        # 시퀀스 번호를 1 증가시킵니다.
        # 같은 밀리초에 여러 추론이 완료되더라도 시퀀스 번호로 구분할 수 있습니다.
        self._inspection_seq += 1

        # 현재 UTC 시각을 "YYYYMMDDHHMMSSmmm" 형식 문자열로 만듭니다.
        # strftime("%Y%m%d%H%M%S%f")는 마이크로초(6자리)까지 출력하므로,
        # [:-3]으로 뒤 3자리를 잘라 밀리초(3자리)까지만 사용합니다.
        # 예: "20260416120000123" (2026년 4월 16일 12시 00분 00초 123밀리초)
        ts = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S%f")[:-3]

        # f-string으로 inspection_id를 조합합니다.
        # {:06d}는 시퀀스 번호를 6자리 0-패딩 정수로 포맷합니다 (예: 1 → "000001").
        # 최종 형식: "station1-20260416120000123-000001"
        return f"station{self._config.station_id}-{ts}-{self._inspection_seq:06d}"

    # -----------------------------------------------------------------------
    # _handle_model_reload() 메서드
    # -----------------------------------------------------------------------
    # 목적: 메인 서버에서 MODEL_RELOAD_CMD(모델 재로드 명령)가 수신되었을 때 호출되는
    #       콜백 함수입니다. 새 모델 파일 경로로 AI 모델을 다시 로드합니다.
    #       이를 통해 서버를 재시작하지 않고도 모델을 업데이트할 수 있습니다.
    #
    # 필터/라우팅:
    #   - station_id 가 자신과 다르면 무시 (메인서버는 전 추론서버에 브로드캐스트)
    #   - Station2 이중모델(YOLO+PatchCore)의 경우 model_type 으로 슬롯 구분:
    #       model_type="YOLO11"   → self._config.model_path (YOLO 슬롯)
    #       model_type="PatchCore"→ self._config.patchcore_model_path (PatchCore 슬롯)
    #   - Station1은 PatchCore 단일이므로 항상 model_path 에 배정
    #
    # 매개변수:
    #   cmd_dict (dict): {"station_id", "model_type", "model_path", "version", ...}
    # 반환값: None
    # -----------------------------------------------------------------------
    def _handle_model_reload(self, cmd_dict: dict) -> None:
        """MODEL_RELOAD_CMD 수신 시 추론기 모델 재로드."""
        # 1) station_id 필터 — 자신의 스테이션이 아니면 무시
        target_station = int(cmd_dict.get("station_id", 0))
        my_station = int(getattr(self._config, "station_id", 0))
        if target_station and target_station != my_station:
            logger.debug("MODEL_RELOAD 무시 | target=%d my=%d",
                         target_station, my_station)
            return

        # 2) 새 모델 경로 / 타입 추출
        model_path = cmd_dict.get("model_path", "")
        model_type = cmd_dict.get("model_type", "")
        if not model_path:
            logger.warning("MODEL_RELOAD: model_path 비어있음 — 재로드만 수행")
            self._inferencer.load_model()
            return

        # 3) 모델 타입에 따라 올바른 슬롯에 배정
        #    Station2 + "PatchCore" → patchcore_model_path
        #    그 외 (Station1 전체, Station2 YOLO11) → model_path
        is_station2_patchcore = (
            my_station == 2 and model_type.upper().startswith("PATCHCORE")
        )
        if is_station2_patchcore:
            old = getattr(self._config, "patchcore_model_path", "")
            self._config.patchcore_model_path = model_path
            logger.info("Reloading PatchCore slot (station2) | %s → %s",
                        old, model_path)
        else:
            old = getattr(self._config, "model_path", "")
            self._config.model_path = model_path
            logger.info("Reloading main model slot (station=%d, type=%s) | %s → %s",
                        my_station, model_type, old, model_path)

        # 4) 추론기가 양쪽 슬롯(model_path + patchcore_model_path)을 모두 재로드한다.
        #    반대쪽 슬롯은 config 값이 그대로라 동일 파일이 다시 로드될 뿐 영향 없음.
        self._inferencer.load_model()

        # 5) v0.15.0: 활성 모델 DB id 저장 (INSPECT_META 의 model_id 필드용).
        #    MainServer 가 MODEL_RELOAD_CMD 송신 시 "model_db_id" 필드를 포함하면
        #    여기서 추론기에 기록되어 이후 INSPECT_META 에 실려 나간다.
        #    필드가 없으면 0 유지(= 추적 불가 상태, 이전과 동일).
        try:
            new_model_id = int(cmd_dict.get("model_db_id", 0) or 0)
        except (TypeError, ValueError):
            new_model_id = 0
        if new_model_id > 0:
            self._inferencer.active_model_id = new_model_id
            logger.info("active_model_id 갱신: %d (station=%d, type=%s)",
                        new_model_id, my_station, model_type)

    # -----------------------------------------------------------------------
    # _handle_inference_control() 메서드 (v0.14.0)
    # -----------------------------------------------------------------------
    # 목적: 메인서버로부터 INFERENCE_CONTROL_CMD(1020) 를 받아 검사 pause/resume.
    # 매개변수:
    #   cmd_dict (dict): {"action": "pause"|"resume", "request_id": ...}
    # 반환값 (bool): 현재 paused 상태 (True=일시정지, False=실행)
    #
    # 동작:
    #   asyncio.Event 기반 — set() 이면 실행, clear() 면 일시정지.
    #   grab_producer 루프가 _pause_event.wait() 에서 블록됨.
    # -----------------------------------------------------------------------
    def _handle_inference_control(self, cmd_dict: dict) -> bool:
        """검사 pause/resume (v0.14.7 re-simplified).

        정책 변경:
          기존엔 pause 시 camera.stop_grabbing(), resume 시 camera.start_grabbing()
          으로 실제 카메라 HW 를 정지/재개했다. 하지만 Basler Pylon 의 Stop/Start
          사이클을 짧게 반복하면 드라이버 내부 상태가 꼬여서 **두 번째 이후 Start 에서
          프레임이 안 나오는 현상** 이 발생 → "첫번째는 되는데 두번누르면 안됨".

        개선:
          pause/resume 은 **asyncio.Event 토글로만** 제어하고, 카메라는 계속 grab
          상태를 유지한다. Pylon 의 GrabStrategy_LatestImageOnly 는 버퍼에 "최신 1프레임"
          만 남기므로 pause 동안 stale 프레임이 누적되지 않음. resume 순간의 "최신 1장"
          을 먼저 1번 retrieve 한 뒤 이후부터 정상 실시간 grab.

          grab_producer 가 pause_event.wait() 에서 블록되는 동안 Pylon 내부는 계속
          프레임을 찍지만 우리가 RetrieveResult 를 호출 안 하므로 CPU/메모리 부담 없음.
          전력 소모는 약간 있지만 (always-on grab), 안정성이 훨씬 중요.
        """
        action = str(cmd_dict.get("action", "")).lower()
        logger.info("INFERENCE_CONTROL 수신: action=%s", action)
        if action == "pause":
            self._pause_event.clear()
            logger.info("INFERENCE_CONTROL: pause 적용 (grab 루프만 블록, 카메라 HW 는 유지)")
            return True
        elif action == "resume":
            self._pause_event.set()
            logger.info("INFERENCE_CONTROL: resume 적용 (grab 루프 재개)")
            return False
        else:
            logger.warning("INFERENCE_CONTROL: unknown action=%s", action)
            return not self._pause_event.is_set()

    # -----------------------------------------------------------------------
    # _handle_arduino_action() 메서드
    # -----------------------------------------------------------------------
    # 목적: NG(불량) 판정 시 아두이노에 시리얼 명령을 보내 물리적 동작을 수행합니다.
    #       Station1: 서보모터로 불량품을 컨베이어에서 리젝트(제거) + 빨간 LED + 부저 울림
    #       Station2: RGB LED 색상 변경 + LCD 화면에 불량 유형 표시 (작업자에게 알림)
    # 매개변수:
    #   result_dict (dict): 추론 결과 딕셔너리. "defect"(단일 결함) 또는 "defects"(복수 결함) 키를
    #                       포함합니다.
    # 반환값: None
    # -----------------------------------------------------------------------
    def _handle_arduino_action(self, result_dict: dict) -> None:
        """NG 시 Arduino 명령 송신.
        Station1: REJECT (서보모터 리젝트 + 빨간 LED + 부저)
        Station2: ALERT:결함유형 (RGB LED + LCD 불량 유형 표시)
        """
        # 결함 유형 문자열을 가져옵니다. 예: "scratch", "dent" 등.
        # 키가 없으면 빈 문자열("")을 반환합니다.
        defect = result_dict.get("defect", "")

        # Station1인 경우: REJECT 명령을 보냅니다.
        if self._config.station_id == 1:
            # "REJECT:scratch\n" 형식으로 시리얼 명령을 전송합니다.
            # 아두이노는 이 명령을 파싱하여 서보모터를 동작시키고 LED/부저를 켭니다.
            # \n(개행)은 아두이노 시리얼 통신에서 명령 끝을 나타내는 구분자입니다.
            self._serial_ctrl.send_command(f"REJECT:{defect}\n")
        else:
            # Station2인 경우: ALERT 명령을 보냅니다.
            # defects(복수 결함 리스트)를 우선 사용하고, 없으면 defect(단일 결함)을 리스트에 넣습니다.
            # 예: defects가 ["scratch", "dent"]이면 "scratch,dent"로 합칩니다.
            defects = result_dict.get("defects", [defect])

            # 결함 리스트를 쉼표(,)로 합쳐서 문자열로 만듭니다.
            # defects가 비어있으면 defect(단일 결함) 문자열을 사용합니다.
            defect_str = ",".join(defects) if defects else defect

            # "ALERT:scratch,dent\n" 형식으로 시리얼 명령을 전송합니다.
            # 아두이노는 이 명령을 파싱하여 LCD에 결함 유형을 표시하고 RGB LED를 변경합니다.
            self._serial_ctrl.send_command(f"ALERT:{defect_str}\n")

    # -----------------------------------------------------------------------
    # _encode_image() 정적 메서드
    # -----------------------------------------------------------------------
    # 목적: numpy 배열 형태의 이미지를 JPEG 바이트로 인코딩(압축)합니다.
    #       네트워크로 전송하려면 원본 이미지(수 MB)를 압축해야 효율적이기 때문입니다.
    #       JPEG 품질 90%로 설정하여 화질과 파일 크기의 균형을 맞춥니다.
    # 매개변수:
    #   image (Any): 인코딩할 이미지. 보통 numpy ndarray(OpenCV BGR 형식)입니다.
    #                None이면 None을 반환합니다.
    # 반환값: Optional[bytes] - JPEG 인코딩된 바이트. 실패하거나 이미지가 None이면 None.
    # 참고: @staticmethod는 self(인스턴스)를 사용하지 않는 메서드입니다.
    #       인스턴스 상태와 무관한 순수 유틸리티 함수에 사용합니다.
    # -----------------------------------------------------------------------
    # -----------------------------------------------------------------------
    # _load_placeholder_frame() 메서드 (v0.14.2)
    # -----------------------------------------------------------------------
    # 목적: 카메라 미연결 시 grab_producer 가 돌려쓸 "정상 이미지" 1장을 로드한다.
    #
    # 왜: 기존에는 _np.random.randint 로 매번 랜덤 픽셀을 생성했으나,
    #     PatchCore 입장에서 랜덤 이미지는 학습 분포와 완전히 동떨어진 OOD 라
    #     전 영역을 이상으로 판정 → 히트맵/마스크 PNG 압축 효율 최악 → 2~3MB
    #     대용량 NG 패킷이 초당 여러 건 MFC 로 쏟아져 TCP 파싱 경계/CImage 복사
    #     이슈를 유발했다.
    #
    # 구현: data/station{N}/test/ 에서 첫 .bmp/.jpg/.png 파일 1장을 OpenCV 로
    #       읽어 caching. 파일이 없으면 None 을 반환하고 호출자가 회색 fallback.
    #
    # 보안/성능:
    #   - 로드는 프로세스 생애 1회만 발생 (caching)
    #   - config.patchcore_input_size 에 맞춰 리사이즈하지 않고 원본을 사용 —
    #     실제 카메라 프레임과 동일 크기/채널 유지 (추론 전 Inferencer 가 리사이즈).
    # -----------------------------------------------------------------------
    # -----------------------------------------------------------------------
    # _collect_test_images() 메서드 (v0.14.10)
    # -----------------------------------------------------------------------
    # 목적: test_image_dir 폴더에서 이미지 파일 목록을 수집한다.
    #       카메라 없이 MFC 실시간 테스트를 위한 순환 재생 모드 지원.
    # 반환값: 이미지 파일 경로 리스트 (정렬됨, 확장자 .bmp/.jpg/.jpeg/.png 만)
    # -----------------------------------------------------------------------
    def _collect_test_images(self, dir_path: str) -> list:
        from pathlib import Path as _Path
        p = _Path(dir_path)
        # 상대경로인 경우 AiServer 루트 기준으로 해석 (config.json 편의)
        if not p.is_absolute():
            p = _Path(__file__).resolve().parent.parent / dir_path
        if not p.is_dir():
            return []
        exts = (".bmp", ".jpg", ".jpeg", ".png")
        return sorted(f for f in p.iterdir() if f.suffix.lower() in exts)

    # -----------------------------------------------------------------------
    # _load_test_image() 메서드 (v0.14.10)
    # -----------------------------------------------------------------------
    # 목적: 순환 재생 중인 테스트 이미지 1장을 cv2 로 읽어 BGR ndarray 로 반환.
    #       한글 경로 안전하도록 np.fromfile + cv2.imdecode 사용.
    # 반환값: ndarray 또는 None (로드 실패 시)
    # -----------------------------------------------------------------------
    def _load_test_image(self, path):
        import numpy as _np
        try:
            import cv2 as _cv2
        except ImportError:
            return None
        try:
            data = _np.fromfile(str(path), dtype=_np.uint8)
            img = _cv2.imdecode(data, _cv2.IMREAD_COLOR)
            if img is None or img.size == 0:
                return None
            return img
        except Exception as exc:
            logger.warning("테스트 이미지 로드 실패 %s: %s", path, exc)
            return None

    def _load_placeholder_frame(self):
        import numpy as _np
        from pathlib import Path as _Path
        try:
            import cv2 as _cv2
        except ImportError:
            return None

        # v0.14.3: normal/ 우선 탐색 — test/ 에는 NG 이미지가 섞여 있어
        # 불량 이미지를 placeholder 로 고정하면 실제 현장에 투입 전 혼동을 줌.
        # normal/ → test/ 순으로 탐색하여 정상 이미지만 placeholder 로 사용.
        station_id = int(getattr(self._config, "station_id", 1))
        base_dir = _Path(__file__).resolve().parent.parent / "data" / f"station{station_id}"
        candidates = [
            base_dir / "normal",          # 정상 이미지 — OK 판정 보장
            base_dir / "train" / "normal",  # 일부 레포는 train/normal 구조
            base_dir / "test",              # 마지막 fallback (혼합 가능)
        ]
        exts = (".bmp", ".jpg", ".jpeg", ".png")
        for dir_path in candidates:
            if not dir_path.exists():
                continue
            for f in sorted(dir_path.iterdir()):
                if f.suffix.lower() in exts:
                    # np.fromfile + imdecode: 한글 경로 안전 (cv2.imread 는 한글 경로 실패)
                    try:
                        data = _np.fromfile(str(f), dtype=_np.uint8)
                        img = _cv2.imdecode(data, _cv2.IMREAD_COLOR)
                        if img is not None and img.size > 0:
                            logger.info("placeholder 로드: %s (shape=%s)", f.name, img.shape)
                            return img
                    except Exception as exc:
                        logger.warning("placeholder 로드 실패(%s): %s", f.name, exc)
                        continue
        logger.warning("placeholder 이미지 없음 — 회색 fallback 사용")
        return None

    @staticmethod
    def _encode_image(image: Any) -> Optional[bytes]:
        """이미지를 JPEG 바이트로 인코딩."""
        # 이미지가 None이면 (예: 더미 이미지) 바로 None을 반환합니다.
        if image is None:
            return None
        try:
            # OpenCV(cv2) 라이브러리를 임포트합니다.
            # 함수 내부에서 import하는 이유: cv2가 설치되지 않은 환경에서도
            # 이 모듈 전체가 import 실패하지 않도록 하기 위한 방어적 코딩입니다.
            import cv2

            # cv2.imencode(): numpy 배열 이미지를 지정 포맷(.jpg)으로 인코딩합니다.
            # 첫 번째 반환값 ok: 인코딩 성공 여부 (True/False).
            # 두 번째 반환값 buf: 인코딩된 데이터가 담긴 numpy 배열.
            # IMWRITE_JPEG_QUALITY, 90: JPEG 압축 품질을 90%로 설정합니다.
            # 100이면 최고 화질/최대 크기, 0이면 최저 화질/최소 크기입니다.
            ok, buf = cv2.imencode(".jpg", image, [cv2.IMWRITE_JPEG_QUALITY, 90])

            # 인코딩에 성공하면 numpy 배열을 파이썬 bytes 객체로 변환하여 반환합니다.
            # bytes 객체는 네트워크 전송이나 파일 저장에 직접 사용할 수 있습니다.
            if ok:
                return buf.tobytes()
        # 인코딩 중 어떤 에러가 발생해도 (cv2 미설치, 잘못된 이미지 등) None을 반환합니다.
        # 이미지 인코딩 실패가 전체 추론 파이프라인을 죽이면 안 되기 때문입니다.
        except Exception:
            pass

        # 인코딩에 실패한 경우 None을 반환합니다.
        return None

    # -----------------------------------------------------------------------
    # _make_iso_timestamp() 정적 메서드
    # -----------------------------------------------------------------------
    # 목적: 현재 UTC 시각을 ISO 8601 형식 문자열로 반환합니다.
    #       예: "2026-04-16T12:00:00.123+00:00"
    #       DB 저장, 로그, 패킷 데이터에서 시각을 표현할 때 사용합니다.
    #       ISO 8601은 국제 표준 시간 형식으로, 시간대 정보까지 포함하여 혼동이 없습니다.
    # 매개변수: 없음
    # 반환값: str - ISO 8601 형식의 현재 UTC 시각 문자열 (밀리초 정밀도)
    # -----------------------------------------------------------------------
    @staticmethod
    def _make_iso_timestamp() -> str:
        # datetime.now(timezone.utc): 현재 UTC 시각을 datetime 객체로 가져옵니다.
        # .isoformat(timespec="milliseconds"): ISO 8601 문자열로 변환하되, 밀리초 정밀도까지 표시합니다.
        return datetime.now(timezone.utc).isoformat(timespec="milliseconds")

    # -----------------------------------------------------------------------
    # _teardown() 메서드
    # -----------------------------------------------------------------------
    # 목적: 파이프라인 종료 시 사용한 리소스를 정리(cleanup)합니다.
    #       TCP 연결을 닫고, 시리얼 포트를 닫습니다.
    #       리소스를 제대로 닫지 않으면 포트가 점유된 채로 남아 다음 실행 시 에러가 납니다.
    #       finally 블록에서 호출되므로 에러 발생 시에도 반드시 실행됩니다.
    # 매개변수: 없음
    # 반환값: None
    # -----------------------------------------------------------------------
    async def _teardown(self) -> None:
        # TCP 클라이언트의 소켓 연결을 닫습니다. 비동기(async) 방식으로 정리합니다.
        await self._tcp_client.close()
        # 아두이노와의 시리얼 포트를 닫습니다. 동기 방식으로 정리합니다.
        self._serial_ctrl.close()
        # Pylon 카메라 핸들을 닫습니다 (v0.11.0).
        # close() 내부에서 예외를 삼키므로 shutdown 경로의 안정성이 보장됩니다.
        try:
            self._camera.close()
        except Exception as exc:
            logger.warning("카메라 close 중 예외(무시): %s", exc)
