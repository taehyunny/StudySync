"""TrainYolo.py
YOLO11 (Ultralytics) 학습 파이프라인.

이 파일의 역할:
  YOLO11 객체탐지(Object Detection) 모델을 학습시키는 전체 파이프라인을 담당한다.
  Ultralytics 라이브러리를 사용하여 사전학습된 YOLO11 모델을 파인튜닝(전이학습)한다.

YOLO란?
  - "You Only Look Once"의 약자로, 이미지에서 물체의 위치(바운딩 박스)와
    종류(클래스)를 동시에 예측하는 실시간 객체탐지 모델이다.
  - 이미지를 한 번만 보고(one-shot) 모든 물체를 찾아내므로 매우 빠르다.

용도:
  - Station2 조립 검사: cap(뚜껑), label(라벨), liquid_level(액체량) 3가지 클래스 객체탐지

학습 전략:
  - 사전학습 YOLO11n 모델 기반 전이학습 (Transfer Learning)
    - 전이학습: 이미 대규모 데이터로 학습된 모델의 지식을 가져와 우리 데이터에 맞게 미세조정하는 기법
  - 입력 크기: 640x640
  - IoU 기반 정상/불량 판정을 위해 높은 정밀도 필요
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가하여 호환성을 높인다.
from __future__ import annotations

# logging: 프로그램 실행 중 상태 메시지를 기록하는 표준 라이브러리이다.
import logging

# shutil: 파일 복사/이동/삭제를 위한 고수준 유틸리티이다.
# 여기서는 학습된 best.pt 파일을 최종 경로로 복사할 때 사용한다.
import shutil

# datetime: 날짜와 시간을 다루는 클래스이다. 모델 버전명 생성에 사용한다.
from datetime import datetime

# Path: 파일 경로를 객체지향적으로 다루는 클래스이다.
from pathlib import Path

# Callable: 함수를 타입으로 표현할 때 사용한다 (콜백 함수의 타입 힌트).
# Optional: None이 될 수 있는 타입을 표현한다.
from typing import Callable, Optional

# 이 모듈 전용 로거를 생성한다. 로그에 모듈명이 자동으로 표시된다.
logger = logging.getLogger(__name__)


class YoloTrainer:
    """YOLO11 학습 파이프라인 클래스.

    용도:
      YOLO11 객체탐지 모델의 학습 전체 과정을 관리한다.
      사전학습 모델 로드 -> 학습 실행 -> 성능 평가 -> best.pt 저장까지 수행한다.

    매개변수:
      data_yaml: YOLO 학습용 데이터 설정 파일(data.yaml) 경로
      output_dir: 학습된 모델을 저장할 폴더 경로
      base_model: 사전학습 가중치 파일명 (예: "yolo11n.pt")
      input_size: 모델에 입력할 이미지 크기 (가로=세로)
      epochs: 전체 데이터를 반복 학습할 횟수
      batch_size: 한 번에 처리할 이미지 수
      patience: 조기 종료 patience (성능 미개선 허용 epoch 수)
      device: 연산 장치 ("cuda" 또는 "cpu")
      progress_callback: 학습 진행률을 외부에 알려주는 콜백 함수
    """

    # 클래스 변수: 탐지할 클래스(물체 종류) 이름 목록이다.
    # 모든 YoloTrainer 인스턴스가 공유한다.
    # cap: 뚜껑, label: 라벨, liquid_level: 액체 수위
    CLASS_NAMES = ["cap", "label", "liquid_level"]

    def __init__(self,
                 data_yaml: str,            # YOLO 학습 데이터 설정 파일(.yaml) 경로
                 output_dir: str,           # 학습된 모델 저장 폴더 경로
                 base_model: str = "yolo11n.pt",   # 사전학습 모델 파일명 (nano 버전)
                 input_size: int = 640,     # 입력 이미지 크기 (640x640 픽셀)
                 epochs: int = 100,         # 학습 반복 횟수
                 batch_size: int = 16,      # 배치 크기 (한 번에 처리할 이미지 수)
                 patience: int = 20,        # 조기 종료 patience (20 epoch 성능 미개선 시 중단)
                 device: str = "cuda",      # 연산 장치 ("cuda": GPU, "cpu": CPU)
                 progress_callback: Optional[Callable[[dict], None]] = None):  # 진행률 콜백
        # 인스턴스 변수에 매개변수를 저장한다.
        self._data_yaml = data_yaml        # 데이터 설정 파일 경로를 저장한다
        self._output_dir = Path(output_dir)  # 출력 경로를 Path 객체로 변환하여 저장한다
        self._base_model = base_model      # 사전학습 모델 파일명을 저장한다
        self._input_size = input_size      # 입력 이미지 크기를 저장한다
        self._epochs = epochs              # 학습 에폭 수를 저장한다
        self._batch_size = batch_size      # 배치 크기를 저장한다
        self._patience = patience          # 조기 종료 patience를 저장한다
        self._device = device              # 연산 장치를 저장한다
        self._progress_callback = progress_callback  # 콜백 함수를 저장한다

    def train(self) -> dict:
        """YOLO11 학습을 실행하는 메인 메서드.

        용도:
          YOLO11 모델의 전체 학습 과정을 순서대로 실행한다.
          1) 사전학습 모델 로드 -> 2) 학습 실행 -> 3) best.pt 저장 -> 4) 성능 지표 추출

        매개변수:
          없음 (self의 인스턴스 변수를 사용)

        반환값 (dict):
          - success (bool): 학습 성공 여부
          - model_path (str): 저장된 모델 파일 경로 (실패 시 빈 문자열)
          - version (str): 모델 버전 (타임스탬프 기반)
          - accuracy (float): mAP50 정확도 (0.0~1.0)
          - message (str): 결과 설명 메시지
        """
        # 현재 시각을 기반으로 고유한 버전 문자열을 생성한다.
        version = datetime.now().strftime("v%Y%m%d_%H%M%S")

        # 모델 이름을 "station2_yolo11_버전" 형식으로 만든다.
        # YOLO는 Station2(조립 검사)에서만 사용하므로 station2로 고정한다.
        model_name = f"station2_yolo11_{version}"

        try:
            # 진행률 0%: 학습 초기화 시작을 알린다.
            self._report_progress(0, "Initializing YOLO11 training...")

            # Ultralytics 라이브러리에서 YOLO 클래스를 임포트한다.
            # 함수 내부에서 임포트하는 이유: ultralytics가 설치되지 않은 환경에서도
            # 이 파일을 임포트할 수 있게 하기 위해서이다.
            from ultralytics import YOLO

            # ── 사전학습 모델 로드 ──
            # YOLO(모델파일): 사전학습된 가중치를 불러온다.
            # "yolo11n.pt"는 COCO 데이터셋(80개 클래스)으로 사전학습된 모델이다.
            # 이 모델의 지식(이미지 특징 추출 능력)을 가져와 우리 데이터에 맞게 미세조정한다.
            model = YOLO(self._base_model)

            # 진행률 5%: 학습 시작을 알린다 (총 epoch 수도 표시).
            self._report_progress(5, f"Training YOLO11 for {self._epochs} epochs...")

            # ── 학습 실행 ──
            # 모델 체크포인트와 학습 결과가 저장될 디렉토리를 생성한다.
            save_dir = self._output_dir / model_name
            save_dir.mkdir(parents=True, exist_ok=True)  # 중간 폴더까지 자동 생성

            # YOLO device 변환: Ultralytics는 "auto"를 인식 못 함
            # "auto" → GPU 있으면 "0"(첫번째 GPU), 없으면 "cpu"
            # "cuda" → "0" (Ultralytics는 "cuda" 대신 GPU 번호를 받음)
            # "cpu" → "cpu" (그대로)
            import torch as _torch
            if self._device == "auto":
                yolo_device = "0" if _torch.cuda.is_available() else "cpu"
            elif self._device == "cuda":
                yolo_device = "0"
            else:
                yolo_device = self._device

            # model.train(): YOLO11 학습을 실행한다.
            # 내부적으로 PyTorch 기반 학습 루프가 돌아간다.
            results = model.train(
                data=self._data_yaml,       # 데이터 설정 파일 경로 (클래스명, 이미지 경로 등 정의)
                epochs=self._epochs,        # 전체 데이터를 반복 학습할 횟수 (100회)
                imgsz=self._input_size,     # 입력 이미지 크기 (640x640)
                batch=self._batch_size,     # 배치 크기 (16장씩 묶어서 처리)
                patience=self._patience,    # 조기 종료: 20 epoch 연속 개선 없으면 중단
                device=yolo_device,         # GPU 번호("0") 또는 "cpu"
                project=str(save_dir),      # 결과 저장 프로젝트 폴더
                name="train",              # 결과 저장 서브 폴더 이름
                exist_ok=True,             # 같은 이름 폴더가 있어도 에러 안 남
                verbose=True,              # 학습 중 상세 로그 출력 활성화
                save=True,                 # 체크포인트 자동 저장 활성화
                plots=True,               # 학습 곡선 그래프 자동 생성 활성화
            )

            # 진행률 80%: 모델 평가 단계를 알린다.
            self._report_progress(80, "Evaluating model...")

            # ── best.pt 파일 찾기 ──
            # YOLO의 trainer.save_dir에서 실제 저장 경로를 가져온다.
            # YOLO가 project 경로를 runs/detect/ 아래로 리다이렉트하는 경우가 있어서,
            # 지정한 save_dir과 실제 저장 위치가 다를 수 있다.
            actual_save_dir = None
            if hasattr(results, "save_dir"):
                # results.save_dir: YOLO가 실제로 저장한 경로
                actual_save_dir = Path(results.save_dir)
            elif hasattr(model, "trainer") and hasattr(model.trainer, "save_dir"):
                # model.trainer.save_dir에서 가져올 수도 있다
                actual_save_dir = Path(model.trainer.save_dir)

            # 검색할 경로 후보 목록 (우선순위순)
            candidate_paths = []
            if actual_save_dir:
                candidate_paths.append(actual_save_dir / "weights" / "best.pt")
            candidate_paths.append(save_dir / "train" / "weights" / "best.pt")

            best_pt = None
            for p in candidate_paths:
                if p.exists():
                    best_pt = p
                    break

            if best_pt is None:
                # 후보 경로에 없으면 넓은 범위로 재귀 검색한다
                search_dirs = [save_dir, Path("runs")]
                if actual_save_dir:
                    search_dirs.insert(0, actual_save_dir)
                for search_dir in search_dirs:
                    if search_dir.exists():
                        pt_files = list(search_dir.rglob("best.pt"))
                        if pt_files:
                            best_pt = max(pt_files, key=lambda p: p.stat().st_mtime)
                            logger.info("Found best.pt at: %s", best_pt)
                            break

            if best_pt is None:
                raise FileNotFoundError("best.pt not found after training")

            # ── 최종 경로로 복사 ──
            # best.pt를 깔끔한 이름으로 최종 출력 폴더에 복사한다.
            # 예: ./models/station2_yolo11_v20260416_143052.pt
            final_path = self._output_dir / f"{model_name}.pt"
            shutil.copy2(best_pt, final_path)  # copy2: 메타데이터(수정시간)까지 함께 복사

            # ── 검증 메트릭(성능 지표) 추출 ──
            # mAP50: Mean Average Precision at IoU=0.5
            # 모든 클래스의 평균 정밀도로, 객체탐지 모델의 대표적인 성능 지표이다.
            # 1.0에 가까울수록 좋다.
            accuracy = 0.0  # 기본값 0.0

            if results is not None:
                # results 객체의 형식이 Ultralytics 버전에 따라 다를 수 있으므로
                # 여러 방식으로 값을 추출한다.
                if hasattr(results, "results_dict"):
                    # results_dict에서 mAP50 값을 가져온다.
                    accuracy = results.results_dict.get("metrics/mAP50(B)", 0.0)
                elif hasattr(results, "maps"):
                    # maps 속성에서 평균을 계산한다.
                    accuracy = float(results.maps.mean()) if hasattr(results.maps, "mean") else 0.0

            # 진행률 100%: 학습 완료를 알린다.
            self._report_progress(100, "YOLO11 training complete!")

            # 학습 성공 결과를 딕셔너리로 반환한다.
            return {
                "success": True,                      # 학습 성공
                "model_path": str(final_path),        # 저장된 모델 파일 경로
                "version": version,                   # 모델 버전
                "accuracy": round(accuracy, 4),       # mAP50 정확도 (소수점 4자리)
                "message": f"YOLO11 training complete: {model_name}",  # 결과 메시지
            }

        except ImportError as exc:
            # ultralytics 라이브러리가 설치되지 않았을 때 발생하는 에러를 처리한다.
            # "pip install ultralytics"로 설치해야 한다.
            msg = f"Required package not installed: {exc}"
            logger.error(msg)
            # 실패 결과를 반환한다.
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}
        except Exception as exc:
            # 그 외 모든 예외를 처리한다 (데이터 오류, GPU 메모리 부족 등).
            msg = f"YOLO11 training failed: {exc}"
            logger.exception(msg)  # 에러 메시지 + 스택 트레이스를 함께 기록한다
            # 실패 결과를 반환한다.
            return {"success": False, "model_path": "", "version": version,
                    "accuracy": 0.0, "message": msg}

    def _report_progress(self, progress: int, status: str) -> None:
        """학습 진행 상태를 로그에 기록하고 콜백으로 외부에 알린다.

        용도:
          학습 진행률을 로그에 남기고, 콜백이 등록되어 있으면 운용서버에도 알린다.

        매개변수:
          progress (int): 진행률 (0~100 퍼센트)
          status (str): 현재 단계 설명 문자열

        반환값:
          없음 (None)
        """
        # 로그에 현재 진행 상태를 기록한다.
        logger.info("[YOLO11] %d%% — %s", progress, status)

        # 콜백 함수가 등록되어 있으면 호출하여 진행 정보를 전달한다.
        if self._progress_callback:
            self._progress_callback({
                "station_id": 2,          # YOLO는 Station2 전용이므로 2로 고정
                "model_type": "YOLO11",   # 모델 종류
                "progress": progress,     # 진행률 (0~100)
                "status": status,         # 현재 단계 설명
            })


def _detect_yolo_layout(data_dir: Path) -> tuple[str, str, str | None]:
    """데이터셋 폴더 구조를 자동 감지해 YOLO 가 쓸 train/val/test 상대경로를 반환.

    지원하는 레이아웃 (우선순위 순):
      1) 표준 YOLO:        data_dir/images/train,  data_dir/images/val
      2) Roboflow-valid:   data_dir/train/images,  data_dir/valid/images (+ test/images 옵션)
      3) Roboflow-val:     data_dir/train/images,  data_dir/val/images
      4) Flat:             data_dir/train,         data_dir/val
      5) Flat-valid:       data_dir/train,         data_dir/valid

    하나라도 매칭되면 해당 상대경로를 반환. 하나도 안 맞으면 표준(images/train, images/val)
    으로 폴백 — 이 경우 실제 디렉토리가 없으면 Ultralytics 가 학습 시 에러를 다시 냄.

    Returns:
      (train_rel, val_rel, test_rel_or_None): path 에 대한 상대 경로들
    """
    candidates = [
        ("images/train", "images/val",    None),             # 표준
        ("train/images", "valid/images",  "test/images"),    # Roboflow (valid)
        ("train/images", "val/images",    "test/images"),    # Roboflow-val
        ("train",        "val",           None),             # Flat
        ("train",        "valid",         None),             # Flat-valid
    ]
    for train_rel, val_rel, test_rel in candidates:
        train_p = data_dir / train_rel
        val_p   = data_dir / val_rel
        if train_p.is_dir() and val_p.is_dir():
            # test 는 있으면 포함, 없으면 None
            if test_rel and (data_dir / test_rel).is_dir():
                logger.info("YOLO 레이아웃 감지: %s (train/val/test)", train_rel)
                return train_rel, val_rel, test_rel
            logger.info("YOLO 레이아웃 감지: %s → train=%s val=%s",
                        data_dir.name, train_rel, val_rel)
            return train_rel, val_rel, None

    # 하나도 매칭 안 됨 — 표준 가정으로 폴백 (Ultralytics 가 에러 메시지 내주도록)
    logger.warning("YOLO 레이아웃 매칭 실패 | data_dir=%s — 표준(images/train, images/val) "
                   "폴백. 실제 폴더가 없으면 학습이 실패할 수 있음.", data_dir)
    return "images/train", "images/val", None


def create_data_yaml(data_dir: str, output_path: str) -> str:
    """YOLO 학습용 data.yaml 파일을 자동 생성/덮어쓰기.

    용도:
      YOLO 학습에는 반드시 data.yaml 설정 파일이 필요하다.
      이 파일에는 학습/검증 이미지 경로와 클래스 이름이 정의되어 있다.
      data_dir 의 실제 폴더 구조를 자동 감지해 알맞은 상대경로를 쓴다.

    지원 레이아웃:
      - 표준 YOLO:     images/train, images/val
      - Roboflow:      train/images, valid/images (+ test/images)
      - Flat:          train, val | train, valid

    매개변수:
      data_dir (str): 이미지와 라벨이 있는 데이터 폴더 경로
      output_path (str): 생성할 data.yaml 파일의 저장 경로

    반환값 (str):
      생성된 data.yaml 파일의 절대 경로 문자열
    """
    data_dir = Path(data_dir)

    # 레이아웃 자동 감지 — train/val/test 상대 경로 결정
    train_rel, val_rel, test_rel = _detect_yolo_layout(data_dir)

    # YAML 본문 조립
    yaml_lines = [
        "# YOLO11 Dataset Configuration",
        "# Auto-generated by Factory Training Server (v0.14.7 — 레이아웃 자동 감지)",
        "",
        f"path: {data_dir.resolve()}",
        f"train: {train_rel}",
        f"val: {val_rel}",
    ]
    if test_rel:
        yaml_lines.append(f"test: {test_rel}")
    yaml_lines += [
        "",
        "# Classes",
        "names:",
        "  0: cap",
        "  1: label",
        "  2: liquid_level",
        "",
        "nc: 3",
        "",
    ]
    yaml_content = "\n".join(yaml_lines)

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(yaml_content, encoding="utf-8")

    logger.info("Created data.yaml: %s (train=%s, val=%s, test=%s)",
                output_path, train_rel, val_rel, test_rel or "-")
    return str(output_path)
