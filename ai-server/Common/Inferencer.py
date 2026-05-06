"""Inferencer.py — AI 추론기 구현

이 파일은 실제 AI 모델을 로드하고 이미지를 추론하는 핵심 클래스들을 정의한다.

클래스 구조:
  BaseInferencer           — 추론기 공통 인터페이스 (추상 클래스 역할)
  ├── Station1Inferencer   — 입고검사: PatchCore 이상탐지 (빈 용기 결함 감지)
  └── Station2Inferencer   — 조립검사: YOLO11 객체탐지 + PatchCore 하이브리드

핵심 개념:
  - PatchCore: "정상" 이미지만으로 학습하여 "정상과 다른 것"을 찾는 이상탐지 모델
  - YOLO11: 이미지에서 물체의 위치와 종류를 찾는 객체탐지 모델
  - 하이브리드: YOLO11으로 구조(캡/라벨) 검사 + PatchCore로 표면 품질 검사
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import logging           # 로그 출력
from pathlib import Path  # 파일 경로를 객체로 다루는 유틸리티
from typing import Any, Optional  # Any: 아무 타입, Optional: None 가능

import numpy as np  # 수치 계산 라이브러리 (이미지를 numpy 배열로 다룸)

# OpenCV — 이미지 처리 라이브러리 (없으면 None으로 대체)
try:
    import cv2
except ImportError:
    cv2 = None  # 설치 안 됐으면 이미지 처리 기능이 비활성화됨

# PyTorch — 딥러닝 프레임워크 (없으면 None으로 대체)
try:
    import torch
    # torch.serialization을 미리 import해두어야 하위 모듈로 접근 가능
    import torch.serialization  # noqa: F401
except ImportError:
    torch = None  # 설치 안 됐으면 추론이 더미 모드로 동작

# 이 모듈 전용 로거
logger = logging.getLogger(__name__)


# 이미 safe_globals 등록 여부를 추적하는 플래그 (한 번만 실행)
_SAFE_GLOBALS_REGISTERED = False


def _register_safe_globals() -> None:
    """Anomalib 커스텀 클래스들을 PyTorch safe_globals에 등록한다.

    PyTorch 2.6부터 torch.load의 weights_only 기본값이 True로 바뀌어,
    커스텀 클래스가 포함된 체크포인트를 로드하려면 명시적으로 허용해야 한다.
    이 함수는 Anomalib이 사용하는 클래스들을 허용 리스트에 추가한다.

    한 번만 실행되도록 전역 플래그로 보호한다.
    """
    global _SAFE_GLOBALS_REGISTERED
    if _SAFE_GLOBALS_REGISTERED or torch is None:
        return

    try:
        # torch.serialization은 별도 import 없이 torch 하위 모듈로 접근 가능
        # (import torch.serialization을 함수 안에서 쓰면 scope 문제 발생)
        safe_classes = []

        # anomalib.PrecisionType (가장 자주 오류가 나는 클래스)
        try:
            from anomalib import PrecisionType
            safe_classes.append(PrecisionType)
        except ImportError:
            pass

        # anomalib.TaskType
        try:
            from anomalib import TaskType
            safe_classes.append(TaskType)
        except ImportError:
            pass

        # anomalib.LearningType
        try:
            from anomalib import LearningType
            safe_classes.append(LearningType)
        except ImportError:
            pass

        # PreProcessor, PostProcessor 등 Anomalib의 다른 커스텀 클래스
        try:
            from anomalib.pre_processing import PreProcessor
            safe_classes.append(PreProcessor)
        except ImportError:
            pass

        try:
            from anomalib.post_processing import PostProcessor
            safe_classes.append(PostProcessor)
        except ImportError:
            pass

        if safe_classes:
            torch.serialization.add_safe_globals(safe_classes)
            logger.info("Registered safe_globals: %s",
                        [c.__name__ for c in safe_classes])

        _SAFE_GLOBALS_REGISTERED = True
    except Exception as exc:
        logger.warning("Failed to register safe_globals: %s", exc)


def _resolve_device(device_str: str) -> Any:
    """Config의 device 문자열을 PyTorch의 torch.device 객체로 변환한다.

    Args:
        device_str: "auto", "cuda", "cpu" 중 하나
    Returns:
        torch.device 객체 (예: torch.device("cuda"))
        PyTorch가 없으면 문자열 "cpu" 반환
    """
    if torch is None:
        # PyTorch가 설치되지 않은 환경 → CPU 문자열만 반환
        return "cpu"
    if device_str == "auto":
        # GPU가 있으면 cuda, 없으면 cpu 자동 선택
        return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if device_str == "cuda" and not torch.cuda.is_available():
        # GPU를 요청했지만 사용 불가 → CPU로 대체
        logger.warning("CUDA requested but not available — falling back to CPU")
        return torch.device("cpu")
    return torch.device(device_str)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# BaseInferencer — 추론기 베이스 클래스
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class BaseInferencer:
    """추론기 베이스 클래스.

    Station1Inferencer와 Station2Inferencer의 공통 인터페이스를 정의한다.
    이 클래스를 직접 사용하지 않고, 하위 클래스에서 load_model()과 infer()를 구현한다.

    추론은 동기(sync) 함수로 정의되어 있으며,
    asyncio 환경에서는 loop.run_in_executor()로 별도 스레드에서 실행한다.
    """

    def __init__(self, config: Any = None):
        """초기화.

        Args:
            config: StationConfig 객체 (모델 경로, 임계값 등 설정 포함)
        """
        self._config = config  # 설정 객체 저장
        # v0.15.0: 현재 활성화된 모델의 DB id (INSPECT_META 의 model_id 필드용).
        #   MODEL_RELOAD_CMD(1010) 수신 시 cmd_dict["model_db_id"] 로 세팅.
        #   MainServer 가 아직 이 필드를 보내지 않으면 0 유지(= 추적 불가 상태).
        #   향후 MainServer GuiNotifier 쪽에 model_db_id 동봉 기능 추가 시 자동 동작.
        self.active_model_id: int = 0

    def load_model(self) -> None:
        """모델 파일을 메모리에 로드한다. 하위 클래스에서 반드시 구현해야 한다."""
        raise NotImplementedError

    def _try_load_threshold(self, model_path: str) -> None:
        """모델 파일과 같은 폴더에 저장된 threshold JSON을 자동 로드한다.

        파일 구조:
          models/station1_patchcore_v20260418_150450.ckpt
          models/station1_patchcore_v20260418_150450_threshold.json  ← 자동 탐색

        또는 기본 이름으로 복사한 경우:
          models/station1_patchcore.ckpt
          models/station1_patchcore_threshold.json

        JSON이 있으면 self._threshold를 덮어쓴다.
        """
        import json
        model_path_obj = Path(model_path)
        # JSON 파일 이름 후보 목록 (모델 파일명 + "_threshold.json")
        json_candidates = [
            model_path_obj.with_suffix("").with_name(
                model_path_obj.stem + "_threshold.json"
            ),
        ]

        for json_path in json_candidates:
            if json_path.exists():
                try:
                    with open(json_path, "r", encoding="utf-8") as f:
                        info = json.load(f)
                    auto_threshold = info.get("threshold")
                    if auto_threshold is not None:
                        # 기존 _threshold 속성이 있으면 자동 임계값으로 덮어쓴다
                        if hasattr(self, "_threshold"):
                            self._threshold = float(auto_threshold)
                        elif hasattr(self, "_anomaly_threshold"):
                            self._anomaly_threshold = float(auto_threshold)
                        logger.info(
                            "Auto-loaded threshold from %s: %.4f (F1=%.4f)",
                            json_path.name, auto_threshold, info.get("f1_score", 0)
                        )
                        return
                except Exception as exc:
                    logger.warning("Failed to load threshold JSON: %s", exc)

    def infer(self, image: Any) -> dict:
        """이미지 한 장을 추론한다. 하위 클래스에서 반드시 구현해야 한다.

        Args:
            image: numpy.ndarray (BGR 컬러 이미지) 또는 None
        Returns:
            결과 dict — 최소 키: result("OK"|"NG"), score(float), defect(str)
        """
        raise NotImplementedError


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Station1Inferencer — PatchCore 이상탐지 (입고 검사)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class Station1Inferencer(BaseInferencer):
    """입고 검사용 PatchCore 추론기.

    동작 원리:
      1. 정상 용기 이미지로 학습된 PatchCore 모델을 로드한다.
      2. 새 이미지가 들어오면 정상 패턴과 비교한다.
      3. "정상과 얼마나 다른가"를 수치(anomaly score)로 출력한다.
      4. 이 점수가 임계값(threshold)을 넘으면 NG(불량) 판정.

    결함 유형 분류:
      점수 > 0.8  → crack (크랙/균열)
      점수 > 0.65 → contamination (이물질/오염)
      점수 > 0.5  → scratch (스크래치)
    """

    def __init__(self, config: Any = None):
        """Station1 추론기 초기화.

        Args:
            config: StationConfig 객체
        """
        super().__init__(config)  # 부모 클래스 초기화

        self._model = None       # PatchCore 모델 객체 (load_model 시 채워짐)
        self._transform = None   # 이미지 전처리 파이프라인 (현재 미사용)

        # device 결정: config.device 값에 따라 cuda 또는 cpu
        self._device = _resolve_device(getattr(config, "device", "auto"))

        # 이상 점수 임계값: 이 값을 넘으면 NG 판정
        self._threshold: float = getattr(config, "anomaly_threshold", 0.5)

        # PatchCore 모델 입력 이미지 크기 (정사각형)
        self._input_size: int = getattr(config, "patchcore_input_size", 224)

    def load_model(self) -> None:
        """PatchCore 모델 파일(.ckpt)을 메모리에 로드한다.

        로드 순서:
          1. Anomalib의 Patchcore.load_from_checkpoint() 시도
          2. 실패하면 torch.load() fallback 시도
          3. 모델 파일이 없으면 더미 모드 (항상 OK 반환)
          4. threshold JSON 파일이 있으면 임계값 자동 적용
        """
        # config에서 모델 경로 가져오기
        model_path = getattr(self._config, "model_path", "")

        # 모델 파일이 없으면 더미 모드로 동작
        if not model_path or not Path(model_path).exists():
            logger.warning("Station1 PatchCore model not found: %s — using dummy mode", model_path)
            self._model = None
            return

        # ── threshold JSON 자동 로드 ──
        # 모델 파일과 같은 폴더에 "{모델명}_threshold.json"이 있으면 임계값을 자동 적용한다.
        # 학습 시 자동 탐색된 최적 임계값을 사용하기 위함.
        self._try_load_threshold(model_path)

        try:
            # Anomalib 라이브러리를 사용한 모델 로드 (권장 방법)
            from anomalib.models import Patchcore

            # PyTorch 2.6+: weights_only=True 기본값으로 인한 보안 오류 회피.
            # Anomalib 커스텀 클래스들을 "안전한 글로벌"로 등록한다.
            # 이렇게 하면 load_from_checkpoint 내부의 torch.load가 성공한다.
            _register_safe_globals()

            # 체크포인트 파일에서 모델 복원
            # map_location: GPU/CPU 지정
            self._model = Patchcore.load_from_checkpoint(
                model_path,
                map_location=self._device,
            )
            self._model.to(self._device)  # GPU 또는 CPU로 모델 이동
            self._model.eval()            # 추론 모드로 전환 (학습 모드 비활성화)

            logger.info("Station1 PatchCore model loaded from %s (device=%s)",
                        model_path, self._device)

        except Exception as exc:
            # load_from_checkpoint 실패 시 fallback 시도
            logger.warning("Anomalib load_from_checkpoint failed (%s) — trying torch.load fallback", exc)
            try:
                # weights_only=False로 체크포인트 전체 로드 (hyperparameters 포함)
                checkpoint = torch.load(model_path, map_location=self._device, weights_only=False)

                if isinstance(checkpoint, dict):
                    from anomalib.models import Patchcore
                    # 체크포인트에서 하이퍼파라미터 추출
                    # PatchCore 학습 시 사용한 backbone, layers 등을 복원해야 한다.
                    hparams = checkpoint.get("hyper_parameters", {})
                    # 모델 생성 시 하이퍼파라미터 전달
                    try:
                        model = Patchcore(**hparams) if hparams else Patchcore()
                    except TypeError:
                        # hparams에 예상 못한 키가 있으면 기본 생성
                        model = Patchcore()
                    # state_dict 로드 (memory_bank 포함됨)
                    if "state_dict" in checkpoint:
                        # strict=False: 일부 키 불일치도 허용 (버전 차이 대응)
                        missing, unexpected = model.load_state_dict(
                            checkpoint["state_dict"], strict=False
                        )
                        if missing:
                            logger.warning("Station1 state_dict missing keys: %s", missing[:5])
                        if unexpected:
                            logger.warning("Station1 state_dict unexpected keys: %s", unexpected[:5])
                    self._model = model
                else:
                    self._model = checkpoint

                self._model.to(self._device)
                self._model.eval()
                logger.info("Station1 model loaded via torch.load: %s", model_path)
            except Exception as exc2:
                logger.error("Station1 model load failed: %s", exc2)
                self._model = None

    def infer(self, image: Any) -> dict:
        """PatchCore로 이미지 한 장을 추론한다.

        Args:
            image: numpy.ndarray (BGR) — 카메라에서 캡처한 원본 이미지
                   None이면 더미 결과 반환
        Returns:
            dict:
              result (str): "OK" 또는 "NG"
              score (float): 이상 점수 (0에 가까울수록 정상, 높을수록 이상)
              defect (str): 결함 유형 ("crack", "contamination" 등, 정상이면 빈 문자열)
              heatmap (ndarray|None): 결함 위치를 색상으로 표시한 히트맵 이미지
        """
        # 이미지가 없거나 모델이 없으면 기본값 반환
        if image is None or self._model is None:
            return {"result": "OK", "score": 0.0, "defect": "", "heatmap": None}

        try:
            # 1단계: 이미지 전처리 (크기 변환 + 정규화 + 텐서 변환)
            input_tensor = self._preprocess(image)

            # 2단계: 추론 실행 (그래디언트 계산 불필요 → no_grad로 메모리 절약)
            # Anomalib Lightning 모델의 내부 모델(PatchcoreModel)을 직접 호출하여
            # raw anomaly score와 anomaly_map을 얻는다.
            # pred_mask는 PostProcessor를 수동 호출하여 정확한 이진 마스크를 얻는다.
            with torch.no_grad():
                if hasattr(self._model, "model") and callable(self._model.model):
                    # model.model = PatchcoreModel (torch.nn.Module) — raw 출력
                    output = self._model.model(input_tensor)
                else:
                    # fallback: Lightning 모델 전체 호출
                    output = self._model(input_tensor)

            # 3단계: 모델 출력 파싱 (여러 형식 지원)
            # ── NamedTuple (InferenceBatch 또는 AnomalibModel 내부 출력) ──
            pred_mask = None  # Anomalib이 이미 계산한 픽셀 마스크 (있으면)
            if hasattr(output, "_fields"):
                # NamedTuple: pred_score, anomaly_map, pred_mask 속성 접근
                raw_score = None
                anomaly_map = None
                # pred_score 또는 pred_scores 필드 탐색
                for key in ("pred_score", "pred_scores"):
                    if hasattr(output, key):
                        raw_score = getattr(output, key)
                        break
                # anomaly_map 또는 anomaly_maps 필드 탐색
                for key in ("anomaly_map", "anomaly_maps"):
                    if hasattr(output, key):
                        anomaly_map = getattr(output, key)
                        break
                # pred_mask 필드 (Anomalib이 계산한 이진 마스크)
                for key in ("pred_mask", "pred_masks"):
                    if hasattr(output, key):
                        pred_mask = getattr(output, key)
                        break
                anomaly_score = float(raw_score.max().cpu().item()) if raw_score is not None else 0.0
            elif isinstance(output, dict):
                # Anomalib dict 출력: {"pred_scores": tensor, "anomaly_maps": tensor}
                raw_score = output.get("pred_scores") or output.get("pred_score") or torch.tensor(0.0)
                anomaly_score = float(raw_score.max().cpu().item()
                                      if torch.is_tensor(raw_score)
                                      else raw_score)
                anomaly_map = output.get("anomaly_maps") or output.get("anomaly_map")
            elif isinstance(output, (tuple, list)):
                # 튜플/리스트 출력: (score, anomaly_map)
                anomaly_score = float(output[0].cpu().item()
                                      if torch.is_tensor(output[0])
                                      else output[0])
                anomaly_map = output[1] if len(output) > 1 else None
            else:
                # 단일 값 출력 (점수만)
                anomaly_score = float(output.cpu().item()
                                      if torch.is_tensor(output)
                                      else output)
                anomaly_map = None

            # Anomalib PostProcessor에 raw output을 통과시켜 pred_mask를 정확히 생성한다.
            # 이것이 Anomalib이 학습 중 저장한 이미지와 동일한 마스크를 만드는 방법이다.
            pixel_threshold = None
            processed_mask = None
            if hasattr(self._model, "post_processor") and self._model.post_processor is not None:
                try:
                    pp = self._model.post_processor
                    # PostProcessor를 호출하여 정규화된 pred_mask를 얻는다
                    with torch.no_grad():
                        processed = pp(output)
                    # 처리된 결과에서 pred_mask 추출
                    if hasattr(processed, "pred_mask"):
                        processed_mask = processed.pred_mask
                    elif hasattr(processed, "pred_masks"):
                        processed_mask = processed.pred_masks
                    elif isinstance(processed, dict):
                        processed_mask = processed.get("pred_mask") or processed.get("pred_masks")

                    # pixel_threshold 값도 추출
                    for attr in ("pixel_threshold", "_pixel_threshold"):
                        if hasattr(pp, attr):
                            val = getattr(pp, attr)
                            if torch.is_tensor(val):
                                pixel_threshold = float(val.cpu().item())
                                break
                            elif val is not None:
                                pixel_threshold = float(val)
                                break
                except Exception as exc:
                    logger.debug("PostProcessor call failed: %s", exc)

            # 4단계: 히트맵 생성 (결함 위치를 시각적으로 표시)
            heatmap = None         # 컬러맵 적용된 시각화용 히트맵 (BGR)
            raw_anomaly_map = None # 원본 수치 맵 (마스크 계산용, float32)
            if anomaly_map is not None:
                # 텐서를 numpy 배열로 변환
                if torch.is_tensor(anomaly_map):
                    raw_map = anomaly_map.squeeze().cpu().numpy()
                else:
                    raw_map = np.array(anomaly_map).squeeze()

                # float32로 변환 (bool/int 등 다양한 타입이 올 수 있으므로)
                raw_map = raw_map.astype(np.float32)

                # 원본 이미지 크기로 리사이즈 (raw 수치 보존)
                raw_map = cv2.resize(raw_map, (image.shape[1], image.shape[0]))

                # 외부에서 마스크 계산에 사용할 수 있도록 raw 그대로 저장
                raw_anomaly_map = raw_map

                # 시각화용 컬러맵 히트맵 생성 (0~1 정규화 후 JET 컬러맵 적용)
                vis_map = raw_map.copy()
                h_min, h_max = vis_map.min(), vis_map.max()
                if h_max > h_min:
                    vis_map = (vis_map - h_min) / (h_max - h_min)  # 0~1 정규화
                vis_map = (vis_map * 255).clip(0, 255).astype(np.uint8)
                heatmap = cv2.applyColorMap(vis_map, cv2.COLORMAP_JET)

            # 5단계: OK/NG 판정
            is_ng = anomaly_score > self._threshold  # 임계값 초과 시 불량
            defect_type = self._classify_defect(anomaly_score) if is_ng else ""

            # pred_mask를 numpy 배열로 변환 + 원본 크기로 리사이즈
            pred_mask_np = None
            if processed_mask is not None:
                if torch.is_tensor(processed_mask):
                    m = processed_mask.squeeze().cpu().numpy()
                else:
                    m = np.array(processed_mask).squeeze()
                # bool 또는 float → uint8로 변환
                m = (m.astype(np.float32) > 0.5).astype(np.uint8) * 255
                # 원본 크기로 리사이즈
                if m.shape[:2] != (image.shape[0], image.shape[1]):
                    m = cv2.resize(m, (image.shape[1], image.shape[0]),
                                   interpolation=cv2.INTER_NEAREST)
                pred_mask_np = m

            return {
                "result": "NG" if is_ng else "OK",
                "score": round(anomaly_score, 4),     # 소수점 4자리까지
                "defect": defect_type,
                "heatmap": heatmap,                   # 컬러맵 적용 히트맵 (BGR)
                "raw_anomaly_map": raw_anomaly_map,   # 원본 수치 맵 (마스크 계산용)
                "pred_mask": pred_mask_np,            # Anomalib PostProcessor가 생성한 이진 마스크
                "pixel_threshold": pixel_threshold,   # Anomalib 내부 pixel threshold
            }

        except Exception as exc:
            # 추론 중 에러 발생 시 안전하게 OK 반환 (라인 멈춤 방지)
            logger.exception("Station1 infer error: %s", exc)
            return {"result": "OK", "score": 0.0, "defect": "", "heatmap": None}

    def _preprocess(self, image: np.ndarray) -> torch.Tensor:
        """BGR 이미지를 PatchCore 모델이 받을 수 있는 텐서로 변환한다.

        처리 과정:
          1. 224x224로 리사이즈
          2. BGR → RGB 변환 (PyTorch 모델은 RGB 입력을 기대)
          3. 0~255 → 0.0~1.0 정규화
          4. ImageNet 평균/표준편차로 정규화 (사전학습 모델 표준)
          5. (H,W,C) → (C,H,W) 차원 변환 (PyTorch 규격)
          6. 배치 차원 추가: (C,H,W) → (1,C,H,W)

        Args:
            image: numpy.ndarray (BGR, uint8)
        Returns:
            torch.Tensor — shape (1, 3, 224, 224), float32
        """
        # 입력 크기로 리사이즈
        img = cv2.resize(image, (self._input_size, self._input_size))

        # BGR → RGB 변환 (OpenCV는 BGR, PyTorch 모델은 RGB)
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        # uint8(0~255) → float32(0.0~1.0)
        img = img.astype(np.float32) / 255.0

        # ImageNet 정규화: 각 채널에서 평균을 빼고 표준편차로 나눔
        # 대부분의 사전학습 모델(ResNet 등)이 이 정규화를 기대한다
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)  # R, G, B 각 채널 평균
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32)   # R, G, B 각 채널 표준편차
        img = (img - mean) / std

        # numpy → torch.Tensor 변환 + 차원 순서 변경
        # numpy: (H, W, C) → PyTorch: (C, H, W)
        tensor = torch.from_numpy(img).permute(2, 0, 1).unsqueeze(0)

        # 지정된 디바이스(GPU/CPU)로 이동
        return tensor.to(self._device)

    @staticmethod
    def _classify_defect(score: float) -> str:
        """결함 유형 라벨 반환.

        PatchCore는 정상/이상 여부만 판단하며,
        "crack", "contamination" 등 구체적인 결함 유형은 구분하지 못한다.
        (그러려면 별도의 분류 모델 학습이 필요)

        따라서 NG 판정 시 단순히 "anomaly"(이상)로만 표시한다.

        Args:
            score: 이상 점수 (참고용, 현재 사용 안 함)
        Returns:
            결함 유형 문자열 ("anomaly")
        """
        return "anomaly"


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Station2Inferencer — YOLO11 + PatchCore 하이브리드 (조립 검사)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class Station2Inferencer(BaseInferencer):
    """조립 검사용 YOLO11 + PatchCore 하이브리드 추론기.

    2단계 판정 로직:
      1차 — YOLO11: cap, label, liquid_level 3개 요소의 존재 여부 + 위치 정상 여부
      2차 — PatchCore: 라벨 ROI를 crop하여 표면 품질(번짐/흐림) 이상 검사
      최종 — 둘 중 하나라도 NG이면 최종 NG

    검사 항목:
      - 캡 체결: 캡이 있는지, 정위치에 있는지
      - 라벨 부착: 라벨이 있는지, 기울어지지 않았는지
      - 충전량: 액면이 기준 범위 안에 있는지
      - 라벨 표면: 인쇄 번짐, 흐림, 불균일 여부
    """

    # YOLO11이 탐지할 클래스 이름 매핑 (학습 시 정의한 순서와 동일해야 함)
    CLASS_NAMES = {0: "cap", 1: "label", 2: "liquid_level"}

    # 반드시 탐지되어야 하는 클래스 목록 (하나라도 없으면 NG)
    REQUIRED_CLASSES = {"cap", "label", "liquid_level"}

    def __init__(self, config: Any = None):
        """Station2 추론기 초기화.

        Args:
            config: StationConfig 객체
        """
        super().__init__(config)  # 부모 클래스 초기화

        self._yolo_model = None       # YOLO11 객체탐지 모델
        self._patchcore_model = None  # PatchCore 이상탐지 모델 (라벨 표면용)

        # device 결정
        self._device = _resolve_device(getattr(config, "device", "auto"))

        # YOLO11 설정값
        self._yolo_conf = getattr(config, "yolo_conf_threshold", 0.5)    # 탐지 신뢰도 임계값
        self._yolo_iou = getattr(config, "yolo_iou_threshold", 0.45)     # NMS IoU 임계값
        self._yolo_input_size = getattr(config, "yolo_input_size", 640)  # 입력 이미지 크기

        # PatchCore 설정값
        self._patchcore_input_size = getattr(config, "patchcore_input_size", 224)
        self._anomaly_threshold = getattr(config, "anomaly_threshold", 0.5)

        # 각 요소의 정상 위치 기준 (이미지 비율 기준)
        # 예: 캡은 이미지 상단 0~15% 영역, 좌우 25~75% 영역에 있어야 정상
        self._reference_boxes = {
            "cap":          {"y_min": 0.0,  "y_max": 0.15, "x_min": 0.25, "x_max": 0.75},
            "label":        {"y_min": 0.20, "y_max": 0.75, "x_min": 0.10, "x_max": 0.90},
            "liquid_level": {"y_min": 0.60, "y_max": 0.90, "x_min": 0.15, "x_max": 0.85},
        }

    def load_model(self) -> None:
        """YOLO11 + PatchCore 두 모델을 모두 로드한다."""

        # ── YOLO11 모델 로드 ──
        yolo_path = getattr(self._config, "model_path", "")
        if yolo_path and Path(yolo_path).exists():
            try:
                from ultralytics import YOLO  # Ultralytics YOLO 라이브러리
                self._yolo_model = YOLO(yolo_path)  # .pt 파일에서 모델 로드
                logger.info("Station2 YOLO11 model loaded: %s", yolo_path)
            except Exception as exc:
                logger.error("Station2 YOLO11 load failed: %s", exc)
                self._yolo_model = None
        else:
            logger.warning("Station2 YOLO11 model not found: %s — using dummy mode", yolo_path)

        # ── PatchCore 모델 로드 (라벨 표면 품질 검사용) ──
        pc_path = getattr(self._config, "patchcore_model_path", "")
        if pc_path and Path(pc_path).exists():
            # threshold JSON 자동 로드 (anomaly_threshold 덮어쓰기)
            self._try_load_threshold(pc_path)
            try:
                from anomalib.models import Patchcore
                # Anomalib 커스텀 클래스들을 safe_globals에 등록 (PyTorch 2.6+ 호환)
                _register_safe_globals()
                self._patchcore_model = Patchcore.load_from_checkpoint(
                    pc_path, map_location=self._device,
                )
                self._patchcore_model.to(self._device)
                self._patchcore_model.eval()
                logger.info("Station2 PatchCore model loaded: %s", pc_path)
            except Exception as exc:
                # load_from_checkpoint 실패 → torch.load fallback
                logger.warning("Station2 PatchCore load_from_checkpoint failed (%s) — trying torch.load", exc)
                try:
                    checkpoint = torch.load(pc_path, map_location=self._device, weights_only=False)
                    if isinstance(checkpoint, dict):
                        from anomalib.models import Patchcore as _PC
                        # 하이퍼파라미터 추출 후 모델 생성
                        hparams = checkpoint.get("hyper_parameters", {})
                        try:
                            model = _PC(**hparams) if hparams else _PC()
                        except TypeError:
                            model = _PC()
                        if "state_dict" in checkpoint:
                            model.load_state_dict(checkpoint["state_dict"], strict=False)
                        self._patchcore_model = model
                    else:
                        self._patchcore_model = checkpoint
                    self._patchcore_model.to(self._device)
                    self._patchcore_model.eval()
                    logger.info("Station2 PatchCore loaded via torch.load: %s", pc_path)
                except Exception as exc2:
                    logger.error("Station2 PatchCore load failed: %s", exc2)
                    self._patchcore_model = None
        else:
            logger.warning("Station2 PatchCore model not found: %s — surface check disabled", pc_path)

    def infer(self, image: Any) -> dict:
        """YOLO11 + PatchCore 하이브리드 추론을 수행한다.

        처리 흐름:
          1. YOLO11로 cap/label/liquid_level 탐지 + 위치 판정
          2. 라벨 영역을 crop하여 PatchCore로 표면 품질 검사
          3. 두 결과를 종합하여 최종 판정

        Args:
            image: numpy.ndarray (BGR) — 카메라 원본 이미지
        Returns:
            dict:
              result: "OK" 또는 "NG"
              score: 총합 점수
              defects: 발견된 결함 목록 (예: ["cap_missing", "label_surface_defect"])
              detections: YOLO 탐지 결과 목록
              cap_ok/label_ok/fill_ok: 각 항목 정상 여부
              patchcore_score: 라벨 표면 이상 점수
              bbox_overlay: 바운딩 박스가 그려진 이미지
        """
        if image is None:
            return self._default_result()

        defects: list[str] = []           # 발견된 결함 목록
        detections: list[dict] = []       # YOLO 탐지 결과
        patchcore_score = 0.0             # PatchCore 이상 점수
        bbox_overlay = image.copy()       # 원본 이미지 복사 (바운딩 박스 그리기용)

        # ── 1차: YOLO11 객체탐지 ──
        yolo_ok, yolo_detections, bbox_overlay = self._run_yolo(image, bbox_overlay)
        detections = yolo_detections

        if not yolo_ok:
            # 누락된 요소 확인 (cap/label/liquid_level 중 없는 것)
            detected_classes = {d["class"] for d in detections}
            missing = self.REQUIRED_CLASSES - detected_classes
            for m in missing:
                defects.append(f"{m}_missing")  # 예: "cap_missing"

            # 위치 이탈 확인 (정상 위치에서 벗어난 것)
            for det in detections:
                if not det.get("position_ok", True):
                    defects.append(f"{det['class']}_misaligned")  # 예: "label_misaligned"

        # ── 2차: PatchCore 라벨 표면 이상탐지 ──
        label_roi = self._extract_label_roi(image, detections)  # 라벨 영역 crop
        if label_roi is not None:
            patchcore_score = self._run_patchcore(label_roi)
            if patchcore_score > self._anomaly_threshold:
                defects.append("label_surface_defect")  # 라벨 표면 결함

        # ── 최종 판정 ──
        is_ng = len(defects) > 0  # 결함이 하나라도 있으면 NG
        total_score = max(
            patchcore_score,
            max((d.get("conf", 0) for d in detections), default=0)
        )

        # 각 항목별 정상 여부 판정
        detected_classes = {d["class"] for d in detections}
        cap_ok = ("cap" in detected_classes
                  and "cap_missing" not in defects
                  and "cap_misaligned" not in defects)
        label_ok = ("label" in detected_classes
                    and "label_missing" not in defects
                    and "label_misaligned" not in defects)
        fill_ok = ("liquid_level" in detected_classes
                   and "liquid_level_missing" not in defects)

        # v0.15.4 주석: Station2 는 **YOLO11 단독** 으로 확정되었음.
        #   PatchCore 는 설계상 제거 결정 → 이 dict 에 "heatmap" / "pred_mask" / "anomaly_map"
        #   키가 없는 것은 **영구적**이며 "미구현" 이 아닌 "설계상 배제".
        #   (v0.15.3 주석에서 v0.16 후보로 남겼던 pred_mask 구현 계획은 폐기.)
        #
        #   config.json 의 station2.patchcore_model_path 가 빈 값이면 self._patchcore=None
        #   상태로 load 되고, _run_patchcore 는 호출돼도 0.0 을 반환. total_score 는 YOLO
        #   판정에만 의존. 통신/DB/UI 모두 pred_mask_size=0 을 안전하게 처리.
        #
        #   patchcore_score 필드는 DB assemblies.patchcore_score NOT NULL 호환성 때문에
        #   0.0 값으로 포함 유지. 필드 자체 제거는 DB 마이그레이션 필요해 보류.
        return {
            "result": "NG" if is_ng else "OK",
            "score": round(total_score, 4),
            # v0.14.11: 모든 결함을 쉼표로 join 해서 전달 → MainServer 의 defect_type
            #   컬럼에 전부 저장되고 MFC 에도 그대로 표시된다.
            #   예: "cap_missing,label_misaligned" — 여러 불량이 동시에 있는 경우.
            #   기존엔 defects[0] 만 보내서 DB 에 첫 번째 결함만 남는 문제가 있었다.
            "defect": ",".join(defects) if defects else "",
            "defects": defects,                         # 전체 결함 목록 (배열 형태도 유지)
            "detections": detections,                   # YOLO 탐지 결과
            "patchcore_score": round(patchcore_score, 4),
            "cap_ok": cap_ok,
            "label_ok": label_ok,
            "fill_ok": fill_ok,
            "yolo_detections": detections,
            "bbox_overlay": bbox_overlay,               # 바운딩 박스 오버레이 이미지
            # "pred_mask": (미구현 — 위 주석 참조)
            # "heatmap":   (미구현 — bbox_overlay 가 현재 히트맵 역할 대체)
        }

    # ── YOLO11 객체탐지 실행 ──

    def _run_yolo(self, image: np.ndarray, overlay: np.ndarray
                  ) -> tuple[bool, list[dict], np.ndarray]:
        """YOLO11로 cap/label/liquid_level을 탐지한다.

        Args:
            image: 원본 이미지 (BGR)
            overlay: 바운딩 박스를 그릴 이미지 (원본의 복사본)
        Returns:
            (all_ok, detections, overlay)
            all_ok: 모든 요소가 정상 위치에 있으면 True
            detections: 탐지 결과 리스트
            overlay: 바운딩 박스가 그려진 이미지
        """
        if self._yolo_model is None:
            # 모델이 없으면 검사 생략 (통과 처리)
            return True, [], overlay

        try:
            # YOLO11 추론 실행
            results = self._yolo_model.predict(
                source=image,                  # 입력 이미지
                conf=self._yolo_conf,          # 최소 신뢰도 (이 이상만 탐지)
                iou=self._yolo_iou,            # NMS IoU 임계값
                imgsz=self._yolo_input_size,   # 모델 입력 크기
                verbose=False,                 # 추론 로그 끄기
            )

            # 결과가 없으면 모든 요소 미탐지 → NG
            if not results or len(results) == 0:
                return False, [], overlay

            result = results[0]           # 첫 번째 (유일한) 이미지 결과
            detections: list[dict] = []   # 탐지 결과를 담을 리스트
            all_ok = True                 # 모든 요소가 정상인지 추적
            h, w = image.shape[:2]        # 이미지 높이, 너비

            # 각 탐지된 박스를 순회
            for box in result.boxes:
                cls_id = int(box.cls.cpu().item())         # 클래스 ID (0, 1, 2)
                conf = float(box.conf.cpu().item())        # 신뢰도 (0.0~1.0)
                # 바운딩 박스 좌표: (x1, y1, x2, y2) — 좌상단, 우하단
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy().astype(int).tolist()
                cls_name = self.CLASS_NAMES.get(cls_id, f"class_{cls_id}")

                # 위치가 정상 범위 안에 있는지 IoU로 판정
                position_ok = self._check_position(cls_name, x1, y1, x2, y2, w, h)
                if not position_ok:
                    all_ok = False  # 하나라도 위치 이상이면 전체 NG

                # 탐지 결과 저장
                detections.append({
                    "class": cls_name,
                    "bbox": [x1, y1, x2, y2],
                    "conf": round(conf, 3),
                    "position_ok": position_ok,
                })

                # 오버레이 이미지에 바운딩 박스 그리기
                color = (0, 255, 0) if position_ok else (0, 0, 255)  # 정상=초록, 이상=빨강
                cv2.rectangle(overlay, (x1, y1), (x2, y2), color, 2)
                label_text = f"{cls_name} {conf:.2f}"
                cv2.putText(overlay, label_text, (x1, y1 - 5),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

            # 필수 클래스가 모두 탐지되었는지 확인
            detected_classes = {d["class"] for d in detections}
            if not self.REQUIRED_CLASSES.issubset(detected_classes):
                all_ok = False  # 누락된 요소가 있으면 NG

            return all_ok, detections, overlay

        except Exception as exc:
            logger.exception("YOLO11 inference error: %s", exc)
            return True, [], overlay  # 에러 시 통과 처리 (안전)

    def _check_position(self, cls_name: str, x1: int, y1: int, x2: int, y2: int,
                        img_w: int, img_h: int) -> bool:
        """탐지된 객체가 정상 위치 범위 안에 있는지 판정한다.

        Args:
            cls_name: 클래스 이름 ("cap", "label", "liquid_level")
            x1, y1, x2, y2: 탐지된 바운딩 박스 좌표 (픽셀)
            img_w, img_h: 이미지 너비, 높이
        Returns:
            True이면 정상 위치, False이면 위치 이탈

        v0.14.13: 위치 검증을 전면 비활성화하고 YOLO 탐지 자체 결과만 믿는다.
            배경:
              실 공장 카메라 설치 위치/각도/거리가 학습 데이터와 달라서 IoU 기반
              reference_boxes 검증이 대부분의 정상 병도 misaligned 로 잘못 판정함.
              현 단계에선 "탐지 됐는가/안 됐는가" 만으로 missing vs 검출 판정을 하고,
              위치 이탈 세부 체크는 생략.
            복구 방법:
              카메라 설치 확정 후 실 촬영 이미지로 _reference_boxes 를 재튜닝하면
              (tests/CalibrateReferenceBoxes.py 활용), 아래 return True 를 제거하고
              원래의 IoU 기반 로직을 되살리면 됨.
        """
        return True    # v0.14.13: 위치 체크 비활성 — YOLO 탐지만으로 OK/NG 판정
        # --- 이하 기존 IoU 로직 (백업, 재활성화 시 return True 제거) ---
        # ref = self._reference_boxes.get(cls_name)
        # if ref is None:
        #     return True  # 기준이 없는 클래스는 통과
        # det_box = (x1 / img_w, y1 / img_h, x2 / img_w, y2 / img_h)
        # ref_box = (ref["x_min"], ref["y_min"], ref["x_max"], ref["y_max"])
        # iou = self._compute_iou(det_box, ref_box)
        # return iou > 0.1

    @staticmethod
    def _compute_iou(box_a: tuple, box_b: tuple) -> float:
        """두 박스의 IoU(Intersection over Union)를 계산한다.

        IoU = 겹치는 영역 / 전체 합집합 영역
        값이 1에 가까울수록 두 박스가 일치, 0이면 전혀 겹치지 않음

        Args:
            box_a, box_b: (x_min, y_min, x_max, y_max) — 정규화 좌표 (0~1)
        Returns:
            IoU 값 (0.0 ~ 1.0)
        """
        # 교집합(intersection) 영역 계산
        xa = max(box_a[0], box_b[0])  # 겹치는 영역의 왼쪽 경계
        ya = max(box_a[1], box_b[1])  # 겹치는 영역의 위쪽 경계
        xb = min(box_a[2], box_b[2])  # 겹치는 영역의 오른쪽 경계
        yb = min(box_a[3], box_b[3])  # 겹치는 영역의 아래쪽 경계

        inter = max(0, xb - xa) * max(0, yb - ya)  # 겹치는 넓이 (음수면 0)

        # 각 박스의 넓이
        area_a = (box_a[2] - box_a[0]) * (box_a[3] - box_a[1])
        area_b = (box_b[2] - box_b[0]) * (box_b[3] - box_b[1])

        # 합집합 넓이 = A + B - 겹침
        union = area_a + area_b - inter

        return inter / union if union > 0 else 0.0

    # ── PatchCore 라벨 표면 검사 ──

    def _extract_label_roi(self, image: np.ndarray,
                           detections: list[dict]) -> Optional[np.ndarray]:
        """YOLO가 탐지한 라벨 영역을 원본 이미지에서 잘라낸다(crop).

        Args:
            image: 원본 이미지 (BGR)
            detections: YOLO 탐지 결과 리스트
        Returns:
            라벨 영역 이미지 (numpy.ndarray) 또는 None (라벨 미탐지 시)
        """
        for det in detections:
            if det["class"] == "label":
                x1, y1, x2, y2 = det["bbox"]
                h, w = image.shape[:2]
                # 좌표가 이미지 범위를 벗어나지 않도록 클리핑
                x1 = max(0, x1)
                y1 = max(0, y1)
                x2 = min(w, x2)
                y2 = min(h, y2)
                if x2 > x1 and y2 > y1:
                    return image[y1:y2, x1:x2].copy()  # 해당 영역 잘라내기
        return None  # 라벨이 탐지되지 않음

    def _run_patchcore(self, label_roi: np.ndarray) -> float:
        """라벨 ROI 이미지에 PatchCore 추론을 수행하여 이상 점수를 반환한다.

        Args:
            label_roi: 라벨 영역 이미지 (BGR, 잘라낸 것)
        Returns:
            이상 점수 (float) — 높을수록 표면 결함 가능성 높음
        """
        if self._patchcore_model is None:
            return 0.0  # 모델이 없으면 검사 생략 (정상 처리)

        try:
            # 전처리: 224x224 리사이즈 → RGB 변환 → 정규화 → 텐서 변환
            img = cv2.resize(label_roi, (self._patchcore_input_size, self._patchcore_input_size))
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img = img.astype(np.float32) / 255.0

            # ImageNet 정규화
            mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
            std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
            img = (img - mean) / std

            # numpy → PyTorch 텐서 변환
            tensor = torch.from_numpy(img).permute(2, 0, 1).unsqueeze(0).to(self._device)

            # 추론 실행
            with torch.no_grad():
                output = self._patchcore_model(tensor)

            # 출력에서 이상 점수 추출
            if isinstance(output, dict):
                score = output.get("pred_scores", torch.tensor(0.0))
                return float(score.cpu().item() if torch.is_tensor(score) else score)
            elif isinstance(output, (tuple, list)):
                return float(output[0].cpu().item() if torch.is_tensor(output[0]) else output[0])
            else:
                return float(output.cpu().item() if torch.is_tensor(output) else output)

        except Exception as exc:
            logger.exception("PatchCore label surface infer error: %s", exc)
            return 0.0  # 에러 시 정상 처리

    @staticmethod
    def _default_result() -> dict:
        """이미지가 None일 때 반환하는 기본 결과."""
        return {
            "result": "OK",
            "score": 0.0,
            "defect": "",
            "defects": [],
            "detections": [],
            "patchcore_score": 0.0,
            "cap_ok": True,
            "label_ok": True,
            "fill_ok": True,
            "yolo_detections": [],
            "bbox_overlay": None,
        }
