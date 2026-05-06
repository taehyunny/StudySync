"""TrainPatchcore.py
PatchCore (Anomalib) 학습 파이프라인.

이 파일의 역할:
  이 파일은 PatchCore라는 이상탐지(Anomaly Detection) 모델을 학습시키는 전체 파이프라인을 담당한다.

용도:
  - Station1 입고 검사: 빈 용기 외관 결함 이상탐지
  - Station2 조립 검사: 라벨 표면 품질 이상탐지

PatchCore란?
  - 정상 이미지만으로 학습하는 비지도학습(unsupervised) 기반 이상탐지 모델이다.
  - 불량 이미지를 수집하기 어려운 제조업 환경에서 매우 유용하다.
  - 사전학습된 ResNet의 중간 레이어에서 특징(feature)을 추출하여 "메모리 뱅크"에 저장한다.
  - 추론 시 새 이미지의 특징이 메모리 뱅크와 얼마나 다른지 비교하여 이상 여부를 판단한다.

학습 전략:
  - 정상 이미지만으로 학습 (비지도학습, unsupervised)
  - 백본: 사전학습 ResNet(wide_resnet50_2) 특징 추출기
  - 입력 크기: 224x224
  - 데이터 증강: 회전, 반전, 밝기/대비, Gaussian Noise 등
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가한다.
# 클래스 내부에서 자기 자신을 타입으로 참조할 때 발생하는 오류를 방지해준다.
from __future__ import annotations

# logging: 프로그램 실행 중 상태 메시지를 기록하는 표준 라이브러리이다.
# print() 대신 logging을 사용하면 로그 레벨(INFO, WARNING, ERROR)별로 필터링할 수 있다.
import logging

# json: 최적 임계값을 JSON 파일로 저장/로드하기 위한 표준 라이브러리
import json

# os: 운영체제와 상호작용하는 함수들을 제공한다 (파일 경로, 환경변수 등).
import os

# shutil: 파일 복사, 이동, 삭제 등 고수준 파일 조작 기능을 제공한다.
# 여기서는 학습 완료된 체크포인트 파일을 최종 경로로 복사할 때 사용한다.
import shutil

# datetime: 날짜와 시간을 다루는 클래스이다.
# 모델 버전명에 현재 시각을 포함시켜 고유한 이름을 만들 때 사용한다.
from datetime import datetime

# Path: 파일 경로를 객체지향적으로 다루는 클래스이다.
# 문자열 대신 Path를 사용하면 / 연산자로 경로를 결합할 수 있어 편리하다.
# 예: Path("./data") / "normal" -> Path("./data/normal")
from pathlib import Path

# Any: 어떤 타입이든 될 수 있음을 표현하는 타입 힌트이다.
# Callable: 함수(호출 가능한 객체)를 타입으로 표현할 때 사용한다.
# Optional: 값이 None일 수도 있음을 표현한다. Optional[str] = str 또는 None.
from typing import Any, Callable, Optional

# __name__을 사용해 이 모듈 전용 로거를 생성한다.
# 이렇게 하면 로그 메시지에 모듈 이름이 자동으로 표시되어 디버깅이 쉬워진다.
logger = logging.getLogger(__name__)


class PatchcoreTrainer:
    """PatchCore 학습 파이프라인 클래스.

    용도:
      PatchCore 이상탐지 모델의 학습 전체 과정을 관리한다.
      데이터 로딩 -> 모델 생성 -> 학습(특징 추출) -> 평가 -> 체크포인트 저장까지 수행한다.

    매개변수:
      station_id: 검사 스테이션 번호 (1 또는 2)
      data_dir: 학습 이미지가 있는 폴더 경로
      output_dir: 학습된 모델을 저장할 폴더 경로
      backbone: 특징 추출에 사용할 사전학습 모델 이름
      input_size: 모델에 입력할 이미지 크기 (가로=세로)
      batch_size: 한 번에 처리할 이미지 수
      num_workers: 데이터 로딩 병렬 프로세스 수
      device: 연산 장치 ("cuda" 또는 "cpu")
      progress_callback: 학습 진행률을 외부에 알려주는 콜백 함수
    """

    def __init__(self,
                 station_id: int,           # 검사 스테이션 번호 (1: 입고검사, 2: 조립검사)
                 data_dir: str,             # 학습 이미지 폴더 경로
                 output_dir: str,           # 모델 저장 폴더 경로
                 backbone: str = "wide_resnet50_2",  # 백본 모델 (ImageNet 사전학습된 ResNet 변형)
                 input_size: int = 224,     # 입력 이미지 크기 (224x224 픽셀)
                 batch_size: int = 32,      # 배치 크기 (한 번에 처리할 이미지 수)
                 num_workers: int = 4,      # 데이터 로딩 병렬 워커 수
                 device: str = "cuda",      # 연산 장치 ("cuda": GPU, "cpu": CPU)
                 progress_callback: Optional[Callable[[dict], None]] = None):  # 진행률 콜백 (없으면 None)
        # 인스턴스 변수에 매개변수 값을 저장한다.
        # _로 시작하는 이름은 "외부에서 직접 접근하지 말라"는 파이썬 관례이다 (private 변수).
        self._station_id = station_id          # 스테이션 ID를 저장한다
        self._data_dir = Path(data_dir)        # 문자열 경로를 Path 객체로 변환하여 저장한다
        self._output_dir = Path(output_dir)    # 출력 경로도 Path 객체로 변환한다
        self._backbone = backbone              # 백본 모델 이름을 저장한다
        self._input_size = input_size          # 입력 이미지 크기를 저장한다
        self._batch_size = batch_size          # 배치 크기를 저장한다
        self._num_workers = num_workers        # 워커 수를 저장한다
        self._device = device                  # 연산 장치를 저장한다
        self._progress_callback = progress_callback  # 콜백 함수를 저장한다 (None이면 진행률 알림 안 함)

    def train(self) -> dict:
        """PatchCore 학습을 실행하는 메인 메서드.

        용도:
          PatchCore 모델의 전체 학습 과정을 순서대로 실행한다.
          1) 데이터셋 로딩 -> 2) 모델 생성 -> 3) 학습 -> 4) 평가 -> 5) 체크포인트 저장

        매개변수:
          없음 (self의 인스턴스 변수를 사용)

        반환값 (dict):
          - success (bool): 학습 성공 여부
          - model_path (str): 저장된 모델 파일 경로 (실패 시 빈 문자열)
          - version (str): 모델 버전 (타임스탬프 기반)
          - accuracy (float): 평가 정확도 (AUROC 점수, 0.0~1.0)
          - message (str): 결과 설명 메시지
        """
        # 현재 시각을 기반으로 고유한 버전 문자열을 생성한다.
        # 예: "v20260416_143052" -> 2026년 4월 16일 14시 30분 52초
        version = datetime.now().strftime("v%Y%m%d_%H%M%S")

        # 모델 이름을 "스테이션번호_모델종류_버전" 형식으로 만든다.
        # 예: "station1_patchcore_v20260416_143052"
        model_name = f"station{self._station_id}_patchcore_{version}"

        try:
            # 진행률 0%: 학습 초기화 시작을 알린다.
            self._report_progress(0, "Initializing PatchCore training...")

            # Anomalib 라이브러리에서 필요한 클래스를 임포트한다.
            # 함수 내부에서 임포트하는 이유: Anomalib이 설치되지 않은 환경에서도
            # 이 파일 자체를 임포트할 수 있게 하기 위해서이다 (ImportError를 try-except로 처리).
            from anomalib.data import Folder      # Folder: 폴더 기반 데이터셋 로더
            from anomalib.engine import Engine    # Engine: 학습/평가를 관리하는 엔진
            from anomalib.models import Patchcore # Patchcore: PatchCore 모델 클래스

            # ── 데이터셋 구성 ──
            # Anomalib의 Folder 데이터 모듈은 root 폴더 아래에 normal_dir을 '상대경로'로 찾는다.
            # 예: root="data/station1", normal_dir="normal" → data/station1/normal/ 에서 이미지 로드
            #
            # data_dir이 "data/station1/normal"처럼 이미 normal 폴더를 가리키는 경우,
            # root는 부모 폴더(data/station1), normal_dir은 폴더 이름("normal")으로 분리해야 한다.
            if (self._data_dir / "normal").exists():
                # data_dir 아래에 "normal" 하위 폴더가 있는 경우
                # 예: data_dir="data/station1" → root="data/station1", normal_dir="normal"
                root_dir = self._data_dir
                normal_dir_name = "normal"
            elif self._data_dir.name == "normal":
                # data_dir 자체가 "normal" 폴더인 경우
                # 예: data_dir="data/station1/normal" → root="data/station1", normal_dir="normal"
                root_dir = self._data_dir.parent
                normal_dir_name = "normal"
            else:
                # 그 외: data_dir에 직접 이미지가 있는 경우
                # root를 부모로, 현재 폴더 이름을 normal_dir로 사용한다
                root_dir = self._data_dir.parent
                normal_dir_name = self._data_dir.name

            # ── 불량 이미지 폴더(abnormal) 자동 감지 ──
            # root_dir 아래에 "abnormal" 폴더가 있으면 AUROC 검증에 사용한다.
            # 없으면 기존처럼 정상 이미지로만 학습한다 (정확도는 0으로 나옴).
            abnormal_dir_path = root_dir / "abnormal"
            has_abnormal = abnormal_dir_path.exists() and any(abnormal_dir_path.iterdir())
            abnormal_dir_name = "abnormal" if has_abnormal else None
            if has_abnormal:
                logger.info("Found abnormal_dir: %s — AUROC validation enabled", abnormal_dir_path)
            else:
                logger.warning("No abnormal_dir found — AUROC will be 0.0")

            # 진행률 10%: 데이터셋 로딩 단계를 알린다.
            self._report_progress(10, "Loading dataset...")

            # Anomalib Folder 데이터 모듈을 생성한다.
            # 이 객체가 이미지 파일을 자동으로 읽고, 리사이즈하고, 배치로 묶어준다.
            # Anomalib 버전에 따라 API가 다르므로, 두 버전 모두 지원하는 방식으로 생성한다.
            import inspect  # Folder의 생성자 매개변수를 동적으로 확인하기 위한 표준 라이브러리
            folder_params = inspect.signature(Folder.__init__).parameters  # Folder.__init__의 매개변수 목록

            if "image_size" in folder_params:
                # Anomalib 구 버전 (v0.x ~ v1.0): image_size 매개변수를 직접 받는다
                folder_kwargs = {
                    "name": model_name,
                    "root": str(root_dir),
                    "normal_dir": normal_dir_name,
                    "image_size": (self._input_size, self._input_size),
                    "train_batch_size": self._batch_size,
                    "eval_batch_size": self._batch_size,
                    "num_workers": self._num_workers,
                    "task": "classification",
                }
                # abnormal 폴더가 있으면 추가한다 (AUROC 검증용)
                if abnormal_dir_name:
                    folder_kwargs["abnormal_dir"] = abnormal_dir_name
                datamodule = Folder(**folder_kwargs)
            else:
                # Anomalib 최신 버전 (v1.1+): image_size 제거됨, 다른 방식으로 이미지 크기 지정
                # 기본 매개변수만 사용하여 Folder 데이터 모듈을 생성한다
                folder_kwargs = {
                    "name": model_name,
                    "root": str(root_dir),
                    "normal_dir": normal_dir_name,
                    "train_batch_size": self._batch_size,
                    "eval_batch_size": self._batch_size,
                    "num_workers": self._num_workers,
                }
                # 최신 버전에서 지원하는 매개변수만 선택적으로 추가한다
                if "task" in folder_params:
                    folder_kwargs["task"] = "classification"
                # abnormal 폴더가 있으면 추가한다 (AUROC 검증용)
                if abnormal_dir_name and "abnormal_dir" in folder_params:
                    folder_kwargs["abnormal_dir"] = abnormal_dir_name
                datamodule = Folder(**folder_kwargs)

            # 진행률 20%: 모델 생성 단계를 알린다.
            self._report_progress(20, "Creating PatchCore model...")

            # PatchCore 모델을 생성한다.
            # PatchCore는 사전학습된 CNN의 중간 레이어에서 특징을 추출하여 사용한다.
            # Anomalib 버전에 따라 매개변수 이름이 다르므로 호환 처리한다.
            patchcore_params = inspect.signature(Patchcore.__init__).parameters
            if "layers_to_extract" in patchcore_params:
                # 구 버전: layers_to_extract 매개변수 사용
                model = Patchcore(
                    backbone=self._backbone,
                    layers_to_extract=["layer2", "layer3"],
                )
            elif "layers" in patchcore_params:
                # 최신 버전: layers 매개변수 사용
                model = Patchcore(
                    backbone=self._backbone,
                    layers=["layer2", "layer3"],
                )
            else:
                # 매개변수를 찾을 수 없으면 기본값으로 생성한다
                model = Patchcore(backbone=self._backbone)
            # layer2: 중간 수준의 특징 (텍스처, 패턴)
            # layer3: 높은 수준의 특징 (물체의 부분적 형태)
            # 두 레이어의 특징을 합쳐서 사용하면 다양한 수준의 이상을 탐지할 수 있다.

            # 진행률 30%: 학습(특징 추출) 시작을 알린다.
            self._report_progress(30, "Starting PatchCore training (feature extraction)...")

            # Anomalib Engine으로 학습을 수행한다.
            # 모델 체크포인트가 저장될 디렉토리를 생성한다.
            save_dir = self._output_dir / model_name  # 예: ./models/station1_patchcore_v20260416_143052
            save_dir.mkdir(parents=True, exist_ok=True)  # 중간 폴더까지 자동 생성, 이미 존재해도 에러 안 남

            # Anomalib Engine: PyTorch Lightning 기반의 학습/평가 관리 엔진이다.
            engine = Engine(
                default_root_dir=str(save_dir),  # 체크포인트, 로그 등이 저장될 루트 디렉토리
                max_epochs=1,  # PatchCore는 1 epoch만 학습한다!
                # 왜 1 epoch인가? PatchCore는 "학습"이 아니라 "메모리 뱅크 구축"이기 때문이다.
                # 정상 이미지의 특징을 한 번만 추출하여 메모리에 저장하면 학습이 끝난다.
                # 일반적인 딥러닝처럼 가중치를 반복 업데이트하는 방식이 아니다.
                devices=1,                       # 사용할 디바이스 수 (1개)
                # accelerator: "gpu" 또는 "cpu"를 선택한다
                # "auto"이면 GPU 유무를 자동 판단, "cuda"이면 GPU, "cpu"이면 CPU
                accelerator="auto" if self._device == "auto" else (
                    "gpu" if self._device == "cuda" else "cpu"
                ),
            )

            # 진행률 40%: 실제 학습(메모리 뱅크 구축)을 시작한다.
            self._report_progress(40, "Training PatchCore model...")

            # engine.fit(): 모델 학습을 실행한다.
            # PatchCore의 경우, 정상 이미지에서 특징을 추출하여 메모리 뱅크를 만든다.
            engine.fit(model=model, datamodule=datamodule)

            # 진행률 70%: 모델 평가(테스트)를 시작한다.
            self._report_progress(70, "Evaluating model...")

            # engine.test(): 학습된 모델의 성능을 평가한다.
            # 정상 이미지를 다시 넣어서 얼마나 잘 구분하는지 테스트한다.
            test_results = engine.test(model=model, datamodule=datamodule)

            # 정확도(AUROC) 추출: 테스트 결과에서 성능 지표를 가져온다.
            # AUROC(Area Under ROC Curve): 0.0~1.0 사이 값으로, 1.0에 가까울수록 좋다.
            # 이상탐지에서 가장 많이 사용하는 평가 지표이다.
            accuracy = 0.0  # 기본값 0.0으로 초기화
            if test_results and len(test_results) > 0:
                # 테스트 결과가 있으면 "image_AUROC" 키에서 값을 꺼낸다.
                accuracy = test_results[0].get("image_AUROC", 0.0)

            # 진행률 85%: 모델 체크포인트 저장 단계를 알린다.
            self._report_progress(85, "Saving model checkpoint...")

            # ── 최종 체크포인트 파일 복사 ──
            # Anomalib Engine이 자동으로 저장한 체크포인트 경로를 찾는다.
            ckpt_path = save_dir / "weights" / "lightning" / "model.ckpt"  # 기본 저장 경로
            final_path = self._output_dir / f"{model_name}.ckpt"  # 최종 저장 경로 (깔끔한 이름)

            if ckpt_path.exists():
                # 기본 경로에 체크포인트가 있으면 최종 경로로 복사한다.
                shutil.copy2(ckpt_path, final_path)  # copy2: 메타데이터(수정시간 등)까지 복사
            else:
                # 기본 경로에 없으면, 저장 디렉토리 전체를 재귀 검색하여 .ckpt 파일을 찾는다.
                # rglob: 하위 폴더까지 재귀적으로 패턴에 맞는 파일을 검색한다.
                ckpt_files = list(save_dir.rglob("*.ckpt"))
                if ckpt_files:
                    # 찾은 체크포인트 중 첫 번째를 사용한다.
                    shutil.copy2(ckpt_files[0], final_path)
                else:
                    # 체크포인트가 전혀 없으면 에러를 발생시킨다.
                    raise FileNotFoundError("No checkpoint found after training")

            # ── 최적 임계값 자동 탐색 (abnormal 폴더가 있을 때만) ──
            # abnormal 폴더가 있으면 F1-score를 최대화하는 임계값을 자동 계산한다.
            # 없으면 기본값 50.0을 사용한다.
            optimal_threshold = 50.0  # 기본값
            threshold_info = {"threshold": optimal_threshold, "auto_detected": False}
            if abnormal_dir_name:
                try:
                    self._report_progress(90, "Finding optimal threshold...")
                    optimal_threshold, threshold_info = self._find_optimal_threshold(
                        model, root_dir, normal_dir_name, abnormal_dir_name
                    )
                    logger.info("Optimal threshold found: %.4f (F1=%.4f)",
                                optimal_threshold, threshold_info.get("f1_score", 0))
                except Exception as exc:
                    logger.warning("Threshold auto-detection failed: %s", exc)

            # 임계값을 JSON 파일로 저장 (추론 시 자동 로드됨)
            threshold_path = self._output_dir / f"{model_name}_threshold.json"
            with open(threshold_path, "w", encoding="utf-8") as f:
                json.dump(threshold_info, f, indent=2, ensure_ascii=False)
            logger.info("Threshold saved to: %s", threshold_path)

            # 진행률 100%: 학습 완료를 알린다.
            self._report_progress(100, "Training complete!")

            # 학습 성공 결과를 딕셔너리로 반환한다.
            return {
                "success": True,                      # 학습 성공
                "model_path": str(final_path),        # 저장된 모델 파일 경로
                "version": version,                   # 모델 버전
                "accuracy": round(accuracy, 4),       # 정확도 (소수점 4자리 반올림)
                "message": f"PatchCore training complete: {model_name}",  # 결과 메시지
            }

        except ImportError as exc:
            # Anomalib 라이브러리가 설치되지 않았을 때 발생하는 에러를 처리한다.
            # "pip install anomalib"로 설치해야 한다.
            msg = f"Required package not installed: {exc}"
            logger.error(msg)  # 에러 로그를 남긴다
            # 실패 결과를 반환한다 (프로그램이 중단되지 않음).
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}
        except Exception as exc:
            # 그 외 모든 예외를 처리한다 (데이터 오류, GPU 오류 등).
            msg = f"PatchCore training failed: {exc}"
            logger.exception(msg)  # exception(): 에러 메시지 + 스택 트레이스(발생 위치)를 함께 기록한다
            # 실패 결과를 반환한다.
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}

    def _find_optimal_threshold(self, model: Any, root_dir: Path,
                                normal_dir_name: str,
                                abnormal_dir_name: str) -> tuple[float, dict]:
        """학습된 PatchCore 모델에 대해 최적 임계값(threshold)을 자동 탐색한다.

        용도:
          정상과 불량 이미지를 각각 추론해서 점수 분포를 파악하고,
          F1-score를 최대화하는 임계값을 찾는다.

        알고리즘:
          1. normal/ 폴더의 정상 이미지들 점수 수집
          2. abnormal/ 폴더의 불량 이미지들 점수 수집
          3. 두 분포의 경계에서 여러 임계값 후보를 평가
          4. F1-score(정밀도와 재현율의 조화평균)가 가장 높은 임계값 선택

        매개변수:
          model: 학습 완료된 PatchCore 모델
          root_dir (Path): 데이터 루트 폴더 (normal/abnormal 상위)
          normal_dir_name (str): 정상 이미지 폴더명
          abnormal_dir_name (str): 불량 이미지 폴더명

        반환값:
          tuple[float, dict]:
            - 최적 임계값 (float)
            - 메타 정보 (dict) - threshold, f1_score, 점수 분포 등
        """
        import torch
        import cv2
        import numpy as np

        # 모델을 추론 모드로 전환하고 디바이스 설정
        model.eval()
        device = next(model.parameters()).device

        def _infer_folder(folder: Path) -> list[float]:
            """폴더 내 모든 이미지에 대해 raw anomaly score를 계산한다."""
            scores = []
            # 지원 확장자
            extensions = (".jpg", ".jpeg", ".png", ".bmp")
            images = sorted([p for p in folder.iterdir()
                             if p.suffix.lower() in extensions])

            for img_path in images:
                # 한글 경로 대응 이미지 로드
                try:
                    img = cv2.imdecode(
                        np.fromfile(str(img_path), dtype=np.uint8),
                        cv2.IMREAD_COLOR
                    )
                except Exception:
                    img = cv2.imread(str(img_path))

                if img is None:
                    continue

                # 전처리: 224x224 리사이즈 + RGB 변환 + ImageNet 정규화
                img_resized = cv2.resize(img, (self._input_size, self._input_size))
                img_rgb = cv2.cvtColor(img_resized, cv2.COLOR_BGR2RGB)
                img_norm = img_rgb.astype(np.float32) / 255.0
                mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
                std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
                img_norm = (img_norm - mean) / std
                tensor = torch.from_numpy(img_norm).permute(2, 0, 1).unsqueeze(0).to(device)

                # raw 모델 직접 호출 (PostProcessor 우회)
                with torch.no_grad():
                    if hasattr(model, "model") and callable(model.model):
                        output = model.model(tensor)
                    else:
                        output = model(tensor)

                # pred_score 추출
                raw_score = None
                if hasattr(output, "pred_score"):
                    raw_score = output.pred_score
                elif hasattr(output, "pred_scores"):
                    raw_score = output.pred_scores
                elif isinstance(output, dict):
                    raw_score = output.get("pred_score") or output.get("pred_scores")

                if raw_score is not None and torch.is_tensor(raw_score):
                    scores.append(float(raw_score.max().cpu().item()))

            return scores

        # ── 정상/불량 점수 수집 ──
        normal_dir = root_dir / normal_dir_name
        abnormal_dir = root_dir / abnormal_dir_name

        logger.info("Collecting scores from normal/abnormal folders...")
        normal_scores = _infer_folder(normal_dir)
        abnormal_scores = _infer_folder(abnormal_dir)

        logger.info("Normal: %d samples, range [%.4f, %.4f]",
                    len(normal_scores),
                    min(normal_scores) if normal_scores else 0,
                    max(normal_scores) if normal_scores else 0)
        logger.info("Abnormal: %d samples, range [%.4f, %.4f]",
                    len(abnormal_scores),
                    min(abnormal_scores) if abnormal_scores else 0,
                    max(abnormal_scores) if abnormal_scores else 0)

        # ── F1-score 최대화 임계값 탐색 ──
        # 후보 임계값: 모든 점수를 정렬하여 각 값 사이 중간값들을 시도
        all_scores = sorted(set(normal_scores + abnormal_scores))
        best_threshold = 50.0
        best_f1 = 0.0

        for candidate in all_scores:
            # candidate를 임계값으로 사용했을 때의 혼동 행렬 계산
            # True Positive: 불량을 불량으로 판정 (score > threshold)
            tp = sum(1 for s in abnormal_scores if s > candidate)
            # False Positive: 정상을 불량으로 오판 (score > threshold)
            fp = sum(1 for s in normal_scores if s > candidate)
            # False Negative: 불량을 정상으로 놓침 (score <= threshold)
            fn = sum(1 for s in abnormal_scores if s <= candidate)

            # Precision(정밀도): 불량 판정 중 실제 불량 비율
            precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
            # Recall(재현율): 실제 불량 중 제대로 잡아낸 비율
            recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
            # F1-score: 정밀도와 재현율의 조화평균
            f1 = 2 * precision * recall / (precision + recall) if (precision + recall) > 0 else 0.0

            if f1 > best_f1:
                best_f1 = f1
                best_threshold = candidate

        # 결과를 dict로 정리
        info = {
            "threshold": round(float(best_threshold), 4),
            "f1_score": round(float(best_f1), 4),
            "auto_detected": True,
            "normal_count": len(normal_scores),
            "abnormal_count": len(abnormal_scores),
            "normal_score_range": [
                round(min(normal_scores), 4) if normal_scores else 0,
                round(max(normal_scores), 4) if normal_scores else 0,
            ],
            "abnormal_score_range": [
                round(min(abnormal_scores), 4) if abnormal_scores else 0,
                round(max(abnormal_scores), 4) if abnormal_scores else 0,
            ],
        }

        return best_threshold, info

    def _report_progress(self, progress: int, status: str) -> None:
        """학습 진행 상태를 로그에 기록하고 콜백으로 외부에 알린다.

        용도:
          학습이 얼마나 진행되었는지를 로그에 남기고,
          콜백 함수가 등록되어 있으면 운용서버에도 진행률을 전송한다.

        매개변수:
          progress (int): 진행률 (0~100 퍼센트)
          status (str): 현재 단계 설명 문자열

        반환값:
          없음 (None)
        """
        # 로그에 현재 진행 상태를 기록한다.
        # %d: 정수, %s: 문자열 포맷팅이다.
        logger.info("[Station%d PatchCore] %d%% — %s", self._station_id, progress, status)

        # 콜백 함수가 등록되어 있으면 호출하여 진행 정보를 전달한다.
        if self._progress_callback:
            self._progress_callback({
                "station_id": self._station_id,  # 스테이션 번호
                "model_type": "PatchCore",       # 모델 종류
                "progress": progress,            # 진행률 (0~100)
                "status": status,                # 현재 단계 설명
            })


def augment_dataset(data_dir: str, factor: int = 5) -> None:
    """데이터 증강 함수 (학습 전에 호출하여 이미지 수를 늘린다).

    용도:
      원본 이미지에 다양한 변환(회전, 반전, 밝기 조정, 노이즈 추가)을 적용하여
      학습 데이터의 양을 인위적으로 늘린다.
      데이터가 적은 제조업 환경에서 모델 성능을 향상시키는 핵심 기법이다.

    적용되는 증강 기법:
      - 회전: 0도, 90도, 180도, 270도 중 랜덤 선택
      - 수평/수직 반전: 50% 확률로 적용
      - 밝기/대비 조정: 밝기 -20~+20, 대비 0.8~1.2 범위
      - Gaussian Noise: 50% 확률로 노이즈 추가

    매개변수:
      data_dir (str): 원본 이미지가 있는 폴더 경로
      factor (int): 원본 1장당 생성할 증강 이미지 수 (기본값: 5)

    반환값:
      없음 (None). 증강된 이미지는 data_dir/augmented/ 폴더에 저장된다.
    """
    # OpenCV: 이미지 읽기/쓰기/변환에 사용하는 컴퓨터 비전 라이브러리이다.
    import cv2
    # NumPy: 수치 계산을 위한 라이브러리이다. 이미지를 다차원 배열로 다룬다.
    import numpy as np

    # 문자열 경로를 Path 객체로 변환한다.
    src_dir = Path(data_dir)

    # 지원하는 이미지 형식(jpg, png, bmp)의 파일 목록을 모은다.
    # glob("*.jpg"): 해당 폴더에서 .jpg로 끝나는 모든 파일을 찾는다.
    images = list(src_dir.glob("*.jpg")) + list(src_dir.glob("*.png")) + list(src_dir.glob("*.bmp"))

    # 이미지가 하나도 없으면 경고 로그를 남기고 종료한다.
    if not images:
        logger.warning("No images found in %s for augmentation", data_dir)
        return

    # 증강 이미지를 저장할 하위 폴더를 생성한다.
    aug_dir = src_dir / "augmented"
    aug_dir.mkdir(exist_ok=True)  # 이미 존재해도 에러 안 남

    # 생성된 증강 이미지 수를 세는 카운터이다.
    count = 0

    # 모든 원본 이미지에 대해 반복한다.
    for img_path in images:
        # cv2.imread(): 이미지 파일을 NumPy 배열로 읽어온다.
        # 결과는 (높이, 너비, 채널) 형태의 3차원 배열이다. 채널 순서는 BGR이다.
        img = cv2.imread(str(img_path))

        # 이미지 읽기에 실패하면(손상된 파일 등) 건너뛴다.
        if img is None:
            continue

        # 파일 이름에서 확장자를 제거한 부분을 가져온다.
        # 예: "image001.jpg" -> "image001"
        base_name = img_path.stem

        # factor 만큼 반복하여 증강 이미지를 생성한다.
        for i in range(factor):
            # 원본 이미지를 복사한다. copy()를 안 하면 원본이 변경된다.
            augmented = img.copy()

            # ── 1단계: 랜덤 회전 (0, 90, 180, 270도 중 하나) ──
            # np.random.choice(): 리스트에서 무작위로 하나를 선택한다.
            angle = np.random.choice([0, 90, 180, 270])
            if angle == 90:
                # 시계 방향 90도 회전
                augmented = cv2.rotate(augmented, cv2.ROTATE_90_CLOCKWISE)
            elif angle == 180:
                # 180도 회전 (위아래 좌우 뒤집기)
                augmented = cv2.rotate(augmented, cv2.ROTATE_180)
            elif angle == 270:
                # 반시계 방향 90도 회전 (= 시계 방향 270도)
                augmented = cv2.rotate(augmented, cv2.ROTATE_90_COUNTERCLOCKWISE)
            # angle == 0이면 회전 안 함

            # ── 2단계: 랜덤 반전 ──
            # np.random.random(): 0.0~1.0 사이 랜덤 값을 생성한다.
            if np.random.random() > 0.5:
                # 50% 확률로 수평 반전 (좌우 뒤집기)
                # flip의 두 번째 인자 1은 수평 반전을 의미한다.
                augmented = cv2.flip(augmented, 1)  # 수평 반전
            if np.random.random() > 0.5:
                # 50% 확률로 수직 반전 (상하 뒤집기)
                # flip의 두 번째 인자 0은 수직 반전을 의미한다.
                augmented = cv2.flip(augmented, 0)  # 수직 반전

            # ── 3단계: 밝기/대비 조정 ──
            # alpha: 대비(contrast) 조정 계수. 1.0은 원본, >1.0은 대비 증가, <1.0은 대비 감소.
            alpha = np.random.uniform(0.8, 1.2)  # 0.8~1.2 사이 랜덤 실수
            # beta: 밝기(brightness) 오프셋. 양수면 밝아지고, 음수면 어두워진다.
            beta = np.random.randint(-20, 20)     # -20~19 사이 랜덤 정수
            # convertScaleAbs: 픽셀값 = alpha * 원본 + beta 를 계산하고, 0~255 범위로 자른다.
            augmented = cv2.convertScaleAbs(augmented, alpha=alpha, beta=beta)

            # ── 4단계: Gaussian Noise (가우시안 노이즈 추가) ──
            if np.random.random() > 0.5:
                # 50% 확률로 노이즈를 추가한다.
                # 노이즈: 평균 0, 표준편차 10인 정규분포에서 랜덤 값을 생성한다.
                # 이미지와 같은 크기(shape)의 노이즈 배열을 만든다.
                noise = np.random.normal(0, 10, augmented.shape).astype(np.float32)
                # 노이즈를 이미지에 더한 후, 0~255 범위를 벗어나지 않도록 clip한다.
                # astype(np.float32): 정수 오버플로를 방지하기 위해 실수로 변환 후 계산한다.
                augmented = np.clip(augmented.astype(np.float32) + noise, 0, 255).astype(np.uint8)

            # 증강된 이미지를 파일로 저장한다.
            # 예: "image001_aug003.jpg"
            out_path = aug_dir / f"{base_name}_aug{i:03d}.jpg"  # :03d -> 3자리 정수 (001, 002, ...)
            cv2.imwrite(str(out_path), augmented)  # JPEG 형식으로 저장
            count += 1  # 카운터 증가

    # 증강 완료 로그를 남긴다.
    logger.info("Augmentation complete: %d images generated in %s", count, aug_dir)
