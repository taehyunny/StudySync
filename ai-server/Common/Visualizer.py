"""Visualizer.py — 추론 결과 시각화 유틸리티

역할:
  PatchCore 가 출력한 anomaly map(float 점수 맵) 과 pred mask(이진 영역) 를
  원본 이미지 위에 합성하여 검사원이 한눈에 확인할 수 있는 이미지를 만든다.
  결과는 PNG 바이트로 인코딩되어 TCP 로 메인서버 → 클라이언트에 전달됨.

3장 이미지 파이프라인 (v0.9.0+):
  1) 원본 JPEG  — 카메라 캡처 그대로
  2) 히트맵 PNG — make_heatmap_overlay(original, anomaly_map) 의 결과
                   파랑(정상) → 빨강(이상) 컬러맵 오버레이
  3) 마스크 PNG — make_pred_mask_overlay(original, pred_mask)  의 결과
                   이상 영역 윤곽선을 초록색으로 표시

클라이언트(MFC) 는 세 이미지를 3분할 뷰에 동시 표시하여 "어디가 왜 NG 인지"
를 즉각 파악할 수 있도록 한다.

안전 장치:
  cv2(OpenCV) 가 없는 환경에서는 ImportError 를 삼키고 함수들이 원본을 그대로
  반환 → 시각화 없이도 파이프라인이 돌아가도록 fail-safe 설계.
"""

from __future__ import annotations  # 타입 힌트 지연 평가

from typing import Optional, Tuple  # 타입 힌트

import numpy as np  # 이미지 배열 처리

try:
    import cv2  # OpenCV — 이미지 합성/인코딩
except ImportError:
    cv2 = None  # OpenCV 없는 환경에서 import 에러 방지


def make_heatmap_overlay(original: np.ndarray,
                         anomaly_map: np.ndarray,
                         alpha: float = 0.5) -> np.ndarray:
    """원본 이미지 위에 Anomaly Map 히트맵을 오버레이한 이미지를 생성한다.

    용도:
      검사원이 "어느 영역이 이상 점수가 높은지"를 색상으로 확인할 수 있게 한다.
      파랑(정상) → 초록 → 노랑 → 빨강(이상) 순으로 색상 강도 표시.

    매개변수:
      original (ndarray): 원본 이미지 (BGR, 임의 크기)
      anomaly_map (ndarray): 이상 점수 맵 (float32, 임의 크기)
      alpha (float): 히트맵 투명도 (0=원본만, 1=히트맵만). 기본 0.5.

    반환값:
      ndarray: 합성 이미지 (BGR, 원본과 동일 크기)
    """
    # OpenCV 없으면 원본을 그대로 반환 (fail-safe)
    if cv2 is None:
        return original

    h, w = original.shape[:2]

    # anomaly_map을 원본 크기로 리사이즈
    # (anomaly_map은 모델 출력이라 224x224 등 작은 크기일 수 있음)
    if anomaly_map.shape[:2] != (h, w):
        amap = cv2.resize(anomaly_map.astype(np.float32), (w, h))
    else:
        amap = anomaly_map.astype(np.float32)

    # min-max 정규화로 0~1 범위로 맞춘다 (히트맵 색상 강도 최대화)
    amin, amax = float(amap.min()), float(amap.max())
    if amax > amin:
        amap_norm = (amap - amin) / (amax - amin)
    else:
        # 모든 픽셀이 같은 값이면 0으로 처리 (히트맵 생성 방지)
        amap_norm = np.zeros_like(amap)

    # 0~255 uint8로 변환 후 JET 컬러맵 적용 (파랑→빨강)
    amap_u8 = (amap_norm * 255).clip(0, 255).astype(np.uint8)
    heatmap_colored = cv2.applyColorMap(amap_u8, cv2.COLORMAP_JET)

    # 원본과 히트맵을 alpha 가중치로 합성
    # addWeighted(src1, α1, src2, α2, γ) = src1*α1 + src2*α2 + γ
    overlay = cv2.addWeighted(original, 1.0 - alpha, heatmap_colored, alpha, 0)

    return overlay


def make_pred_mask_overlay(original: np.ndarray,
                           pred_mask: np.ndarray,
                           contour_color: Tuple[int, int, int] = (0, 0, 255),
                           contour_thickness: int = 3) -> np.ndarray:
    """원본 이미지 위에 Pred Mask의 윤곽선을 빨간색으로 그린 이미지를 생성한다.

    용도:
      "AI가 불량이라고 판단한 정확한 영역"을 빨간 테두리로 표시.
      검사원이 불량 위치를 한눈에 파악할 수 있게 한다.

    매개변수:
      original (ndarray): 원본 이미지 (BGR)
      pred_mask (ndarray): 이진 마스크 (0 또는 255, 또는 bool/float)
      contour_color (tuple): 윤곽선 색상 (BGR, 기본 빨강)
      contour_thickness (int): 윤곽선 두께 (픽셀, 기본 3)

    반환값:
      ndarray: 윤곽선이 그려진 이미지 (BGR, 원본과 동일 크기)
    """
    if cv2 is None:
        return original

    h, w = original.shape[:2]
    panel = original.copy()  # 원본을 복사해서 그 위에 그림

    # pred_mask가 None이면 윤곽선 없이 원본만 반환
    if pred_mask is None:
        return panel

    # bool/float → uint8로 변환 (threshold 0.5 기준 이진화)
    mask = (pred_mask.astype(np.float32) > 0.5).astype(np.uint8) * 255

    # 원본 크기로 리사이즈 (nearest: 이진값 유지)
    if mask.shape[:2] != (h, w):
        mask = cv2.resize(mask, (w, h), interpolation=cv2.INTER_NEAREST)

    # 마스크의 바깥쪽 윤곽선만 추출 (내부 구멍 제외)
    # RETR_EXTERNAL: 최외곽만, CHAIN_APPROX_SIMPLE: 꼭짓점만 저장 (메모리 절약)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL,
                                    cv2.CHAIN_APPROX_SIMPLE)

    # 윤곽선 그리기 (색상은 BGR, 두께 지정)
    cv2.drawContours(panel, contours, -1, contour_color, contour_thickness)

    return panel


def encode_image(image: Optional[np.ndarray],
                 ext: str = ".jpg",
                 quality: int = 90) -> Optional[bytes]:
    """numpy 이미지를 JPEG/PNG 바이트로 인코딩한다.

    용도:
      TCP 전송용 바이트 스트림 변환.
      네트워크 전송을 위해 numpy 배열을 압축된 바이너리로 변환한다.

    매개변수:
      image (ndarray): 이미지 (BGR 또는 단일 채널)
      ext (str): 확장자 (".jpg" 또는 ".png")
      quality (int): JPEG 품질 (1~100, 기본 90)

    반환값:
      bytes: 인코딩된 바이트열. 실패 시 None.
    """
    if image is None or cv2 is None:
        return None

    try:
        # JPEG이면 품질 파라미터 설정, PNG면 무손실이라 파라미터 불필요
        if ext.lower() in (".jpg", ".jpeg"):
            params = [cv2.IMWRITE_JPEG_QUALITY, quality]
        else:
            params = []

        # imencode: 이미지 → 바이트 배열 (확장자 문자열 포함)
        ok, buf = cv2.imencode(ext, image, params)
        if ok:
            return buf.tobytes()  # numpy array → bytes
    except Exception:
        pass
    return None
