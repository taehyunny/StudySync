"""Config.py — 추론 서버 설정 (config.json 기반)

이 파일은 Common/ConfigLoader.py를 통해 프로젝트 루트의
config/config.json에서 설정을 읽어와 StationConfig 객체를 생성한다.

기존의 하드코딩 dataclass는 유지하되, 값은 config.json에서 로드된다.

왜 config.json을 쓰는가:
  - 코드를 수정하지 않고도 IP, 포트, 임계값 등을 변경할 수 있다.
  - 개발/운영 환경별로 다른 설정을 적용하기 쉽다.
  - 여러 서버(학습, 추론1, 추론2)가 같은 설정 파일을 공유할 수 있다.
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가하게 만든다.
# Python 3.10 미만에서도 최신 타입 힌트 문법을 쓸 수 있게 한다.
from __future__ import annotations

# dataclass: 클래스를 간결하게 정의하는 데코레이터이다.
# __init__, __repr__ 등을 자동 생성해주어 보일러플레이트 코드를 줄인다.
# field: dataclass에서 기본값에 가변 객체(list 등)를 쓸 때 사용한다.
from dataclasses import dataclass, field

# Optional: 값이 None일 수도 있음을 표현하는 타입 힌트이다.
# 예: Optional[str] = str 또는 None.
from typing import Optional

# ConfigLoader: config/config.json 파일을 읽어와 값을 제공하는 유틸리티 클래스.
from Common.ConfigLoader import ConfigLoader


@dataclass  # 이 데코레이터를 붙이면 __init__ 등이 자동 생성된다.
class StationConfig:
    """스테이션(추론서버) 단위 설정을 담는 클래스.

    직접 생성보다는 from_json(station_id)으로 config.json에서 로드 권장.
    각 필드에 기본값이 지정되어 있어 일부만 바꿔서 사용할 수도 있다.
    """

    # ── 기본 서버 정보 ──
    station_id: int = 1                           # 이 추론서버의 스테이션 번호 (1=입고검사, 2=조립검사)
    main_server_host: str = "10.10.10.130"        # 운용서버(메인서버)의 IP 주소
    main_server_port: int = 9000                  # 운용서버의 TCP 포트 번호

    # ── Pylon 카메라 설정 ──
    # camera_serial  : 특정 카메라 시리얼 번호(Station별 고정). 빈 값이면 첫 번째 발견 카메라 사용.
    # camera_enabled : false 이면 더미 이미지 모드(랜덤 np.ndarray) — 개발/CI 환경용.
    # camera_fps     : grab 주기(초). 0.5 = 2fps.
    camera_serial: str = ""
    camera_enabled: bool = True
    camera_fps: float = 2.0

    # v0.14.10: 테스트 모드 — camera_enabled=false 인 상태에서 이 경로가 설정돼 있으면
    #   해당 폴더의 이미지들을 카메라 대신 순환 재생한다. MFC/Arduino 실운영 파이프라인을
    #   카메라 없이 검증할 때 사용. 예: "./data/station2/yolo/images/test"
    test_image_dir: str = ""

    # ── AI 모델 경로 ──
    model_path: str = ""                          # 메인 모델 파일 경로 (Station1: PatchCore .ckpt / Station2: YOLO11 .pt)
    patchcore_model_path: str = ""                # Station2 전용: 라벨 표면 품질 검사용 PatchCore 모델 경로

    # ── 추론 디바이스 설정 ──
    # "auto" : GPU가 있으면 GPU(cuda), 없으면 CPU 자동 선택
    # "cuda" : GPU 강제 사용 (없으면 CPU로 fallback)
    # "cpu"  : CPU 강제 사용
    device: str = "auto"

    # ── PatchCore 이상탐지 설정 ──
    anomaly_threshold: float = 0.5                # 이상 점수가 이 값을 넘으면 NG(불량) 판정
    patchcore_input_size: int = 224               # PatchCore 모델 입력 이미지 크기 (224x224 픽셀)

    # ── YOLO11 객체탐지 설정 (Station2 전용) ──
    yolo_conf_threshold: float = 0.5              # YOLO 탐지 신뢰도 임계값 (이 값 이상인 탐지만 유효)
    yolo_iou_threshold: float = 0.45              # YOLO NMS(Non-Maximum Suppression)에서 겹치는 박스 제거 기준
    yolo_input_size: int = 640                    # YOLO 모델 입력 이미지 크기 (640x640 픽셀)

    # ── 비동기 큐/워커 설정 ──
    grab_queue_max: int = 16                      # 카메라에서 캡처한 이미지를 담는 큐의 최대 크기
    inference_workers: int = 1                    # 동시에 추론을 수행하는 워커(코루틴) 수
    sender_workers: int = 1                       # 메인서버로 결과를 전송하는 워커(코루틴) 수

    # ── Arduino 시리얼 통신 설정 ──
    arduino_port: Optional[str] = None            # 아두이노 시리얼 포트 (예: "COM3", "/dev/ttyUSB0", None이면 미사용)
    arduino_baud: int = 9600                      # 아두이노 시리얼 통신 속도 (baud rate)

    @classmethod
    def from_json(cls, station_id: int) -> "StationConfig":
        """config.json에서 스테이션 설정을 로드하는 클래스 메서드.

        용도:
          config/config.json 파일의 값으로 StationConfig 객체를 생성한다.
          예: from_json(1) → Station1 설정을 config.json에서 읽어와 객체 생성.

        매개변수:
          station_id (int): 스테이션 번호 (1 또는 2)

        반환값:
          StationConfig: config.json 값으로 채워진 설정 객체
        """
        # ConfigLoader가 아직 로드 안 됐으면 로드한다 (lazy loading).
        # _config가 None이면 처음 호출이라는 뜻.
        if ConfigLoader._config is None:
            ConfigLoader.load()

        # JSON 경로 prefix 구성 (예: "ai_server.station1")
        # 이렇게 하면 station_id별로 다른 설정을 찾을 수 있다.
        prefix = f"ai_server.station{station_id}"

        # cls(...): StationConfig 객체를 생성하여 반환한다.
        # 각 필드 값을 ConfigLoader로부터 조회한다 (없으면 기본값 사용).
        return cls(
            station_id=station_id,
            # 네트워크 설정: 모든 스테이션이 공유
            main_server_host=ConfigLoader.get("network.main_server_host", "10.10.10.130"),
            main_server_port=ConfigLoader.get_int("network.main_server_ai_port", 9000),

            # AI 모델 경로 및 디바이스
            model_path=ConfigLoader.get(f"{prefix}.model_path", ""),
            patchcore_model_path=ConfigLoader.get(f"{prefix}.patchcore_model_path", ""),
            device=ConfigLoader.get(f"{prefix}.device", "auto"),

            # PatchCore 설정
            anomaly_threshold=ConfigLoader.get_float(f"{prefix}.anomaly_threshold", 0.5),
            patchcore_input_size=ConfigLoader.get_int(f"{prefix}.patchcore_input_size", 224),

            # YOLO11 설정 (Station2에서만 주로 사용)
            yolo_conf_threshold=ConfigLoader.get_float(f"{prefix}.yolo_conf_threshold", 0.5),
            yolo_iou_threshold=ConfigLoader.get_float(f"{prefix}.yolo_iou_threshold", 0.45),
            yolo_input_size=ConfigLoader.get_int(f"{prefix}.yolo_input_size", 640),

            # 비동기 큐/워커 설정
            grab_queue_max=ConfigLoader.get_int(f"{prefix}.grab_queue_max", 16),
            inference_workers=ConfigLoader.get_int(f"{prefix}.inference_workers", 1),
            sender_workers=ConfigLoader.get_int(f"{prefix}.sender_workers", 1),

            # 카메라 설정 (v0.11.0: 실제 Pylon 연동 — 미연결/라이브러리 없으면 더미로 폴백)
            camera_serial=ConfigLoader.get(f"{prefix}.camera_serial", ""),
            camera_enabled=ConfigLoader.get_bool(f"{prefix}.camera_enabled", True),
            camera_fps=ConfigLoader.get_float(f"{prefix}.camera_fps", 2.0),

            # v0.14.10: 카메라 미연결 시 테스트 이미지 폴더 순환
            test_image_dir=ConfigLoader.get(f"{prefix}.test_image_dir", ""),

            # Arduino 시리얼 통신 설정 (v0.14.8: config.json 에서 로드 누락되던 버그 수정)
            # NG 판정 시 REJECT/ALERT 명령을 시리얼로 Arduino 에 전송한다.
            # arduino_port 값:
            #   Windows → "COM3", "COM4" 등
            #   Linux   → "/dev/ttyUSB0", "/dev/ttyACM0" 등
            #   null/미설정 → Arduino 미사용 (no-op, 예외 없음)
            arduino_port=ConfigLoader.get(f"{prefix}.arduino_port", None),
            arduino_baud=ConfigLoader.get_int(f"{prefix}.arduino_baud", 9600),
        )
