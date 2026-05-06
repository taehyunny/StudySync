"""TrainingConfig.py — 학습 서버 설정 (config.json 기반)

이 파일은 프로젝트 루트의 config/config.json에서 학습 관련 설정을 로드한다.
AiServer/Common/ConfigLoader.py를 사용하여 중앙화된 설정 파일을 공유한다.

왜 필요한가:
  - AI 학습 서버는 GPU, 학습 하이퍼파라미터 등 복잡한 설정이 많다.
  - 코드 수정 없이 설정만 바꿔서 재학습할 수 있도록 JSON 파일로 분리.
  - 학습/추론 서버가 같은 설정을 참조할 수 있도록 중앙화.
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가 (Python 3.10 미만 호환)
from __future__ import annotations

# sys: 파이썬 모듈 검색 경로를 동적으로 추가하기 위해 사용
import sys

# dataclass: 클래스의 __init__ 등을 자동 생성하는 데코레이터
from dataclasses import dataclass

# Path: 객체지향 경로 처리 라이브러리
from pathlib import Path

# Optional: 값이 None일 수 있음을 표현하는 타입 힌트
from typing import Optional

# ── 상위 패키지 접근을 위한 sys.path 조정 ──
# Training/TrainingConfig.py에서 Common/ConfigLoader.py를 import 하려면
# AiServer/ 폴더가 파이썬 모듈 검색 경로에 포함되어야 한다.
# 이렇게 하지 않으면 "from Common.ConfigLoader import ..." 가 실패한다.

# 현재 파일의 디렉토리 (AiServer/Training/)
_THIS_DIR = Path(__file__).resolve().parent

# 한 단계 위 디렉토리 (AiServer/)
_AISERVER_DIR = _THIS_DIR.parent

# sys.path에 아직 없으면 추가 (중복 추가 방지)
if str(_AISERVER_DIR) not in sys.path:
    # insert(0, ...): 맨 앞에 추가하여 우선순위를 높임
    sys.path.insert(0, str(_AISERVER_DIR))

# 경로 설정 후에 Common.ConfigLoader를 import 할 수 있다.
from Common.ConfigLoader import ConfigLoader


@dataclass  # 이 데코레이터가 __init__, __repr__ 등을 자동 생성
class TrainingConfig:
    """학습 서버 설정 데이터 클래스.

    직접 생성보다 from_json() 클래스 메서드로 config.json에서 로드 권장.
    각 필드에 기본값이 있어 일부만 변경해서 사용할 수도 있다.
    """

    # ── TCP 통신 설정 ──
    # listen_host: 이 학습서버가 명령을 받을 IP 주소
    # "0.0.0.0"은 모든 네트워크 인터페이스에서 수신 (외부 접속 허용)
    listen_host: str = "0.0.0.0"
    # listen_port: 학습서버가 명령을 받는 TCP 포트 (9100)
    listen_port: int = 9100
    # main_server_host: 운용서버 IP (학습 완료 통보 전송용)
    main_server_host: str = "10.10.10.130"
    # main_server_port: 운용서버 TCP 포트
    main_server_port: int = 9000

    # ── GPU 설정 ──
    # device: 학습에 사용할 디바이스
    # "cuda": GPU 사용, "cpu": CPU 사용, "auto": 자동 선택
    device: str = "cuda"
    # gpu_id: 여러 GPU가 있을 때 사용할 번호 (0=첫 번째 GPU)
    gpu_id: int = 0

    # ── 데이터 경로 ──
    # data_root: 학습 데이터 루트 폴더 (하위에 station1/, station2/ 구조)
    data_root: str = "./data"
    # model_output_dir: 학습 완료된 모델 저장 폴더
    model_output_dir: str = "./models"

    # ── PatchCore 학습 설정 ──
    # patchcore_backbone: 특징 추출에 사용할 사전학습 모델
    # wide_resnet50_2: 표준 ResNet50보다 채널이 2배 넓은 변형 (정확도 ↑, 속도 ↓)
    patchcore_backbone: str = "wide_resnet50_2"
    # patchcore_input_size: 모델 입력 이미지 크기 (224x224 픽셀)
    patchcore_input_size: int = 224
    # patchcore_batch_size: 한 번에 처리할 이미지 수 (GPU 메모리에 맞게 조정)
    patchcore_batch_size: int = 32
    # patchcore_num_workers: 데이터 로딩 병렬 프로세스 수
    patchcore_num_workers: int = 4

    # ── YOLO11 학습 설정 ──
    # yolo_base_model: 사전학습된 YOLO 모델 파일 (전이학습 시작점)
    # yolo11n.pt = YOLO11 nano (작고 빠름), yolo11s/m/l/x 순으로 커짐
    yolo_base_model: str = "yolo11n.pt"
    # yolo_input_size: YOLO 입력 이미지 크기 (보통 640x640)
    yolo_input_size: int = 640
    # yolo_epochs: 전체 데이터를 반복 학습할 횟수
    yolo_epochs: int = 100
    # yolo_batch_size: YOLO 학습 배치 크기
    yolo_batch_size: int = 16
    # yolo_patience: 조기 종료(early stopping) patience
    # N epoch 연속 성능 향상 없으면 학습 중단 (과적합 방지)
    yolo_patience: int = 20

    # ── 증강/배포 설정 ──
    # augmentation_factor: 원본 대비 증강 배수 (5이면 원본 1장당 5장의 변형 생성)
    augmentation_factor: int = 5
    # deploy_dir: 학습된 모델을 추론서버로 배포할 때 사용할 폴더
    deploy_dir: str = "./deploy"

    @classmethod
    def from_json(cls) -> "TrainingConfig":
        """config.json에서 학습 설정을 로드하는 클래스 메서드.

        용도:
          config/config.json의 "training" 섹션에서 값을 읽어와
          TrainingConfig 객체를 생성한다.

        매개변수: 없음 (클래스 메서드)

        반환값:
          TrainingConfig: config.json 값으로 채워진 설정 객체
        """
        # ConfigLoader가 아직 로드 안 됐으면 로드 (lazy loading)
        if ConfigLoader._config is None:
            ConfigLoader.load()

        # cls(...)로 TrainingConfig 인스턴스 생성
        # 각 필드 값을 ConfigLoader로 조회 (없으면 기본값 사용)
        return cls(
            # ── TCP ──
            # training.listen_host가 없으면 "0.0.0.0" 기본값 사용
            listen_host=ConfigLoader.get("training.listen_host", "0.0.0.0"),
            # network.training_server_port가 없으면 9100 기본값
            listen_port=ConfigLoader.get_int("network.training_server_port", 9100),
            # 운용서버 정보 (네트워크 설정에서 공유)
            main_server_host=ConfigLoader.get("network.main_server_host", "10.10.10.130"),
            main_server_port=ConfigLoader.get_int("network.main_server_ai_port", 9000),

            # ── GPU ──
            device=ConfigLoader.get("training.device", "cuda"),
            gpu_id=ConfigLoader.get_int("training.gpu_id", 0),

            # ── 데이터 경로 ──
            data_root=ConfigLoader.get("training.data_root", "./data"),
            model_output_dir=ConfigLoader.get("training.model_output_dir", "./models"),

            # ── PatchCore 설정 ──
            patchcore_backbone=ConfigLoader.get("training.patchcore_backbone", "wide_resnet50_2"),
            patchcore_input_size=ConfigLoader.get_int("training.patchcore_input_size", 224),
            patchcore_batch_size=ConfigLoader.get_int("training.patchcore_batch_size", 32),

            # ── YOLO11 설정 ──
            yolo_base_model=ConfigLoader.get("training.yolo_base_model", "yolo11n.pt"),
            yolo_input_size=ConfigLoader.get_int("training.yolo_input_size", 640),
            yolo_epochs=ConfigLoader.get_int("training.yolo_epochs", 100),
            yolo_batch_size=ConfigLoader.get_int("training.yolo_batch_size", 16),
            yolo_patience=ConfigLoader.get_int("training.yolo_patience", 20),

            # ── 증강/배포 ──
            augmentation_factor=ConfigLoader.get_int("training.augmentation_factor", 5),
            deploy_dir=ConfigLoader.get("training.deploy_dir", "./deploy"),
        )
