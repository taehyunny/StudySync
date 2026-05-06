"""PylonCamera.py — Basler Pylon 카메라 래퍼 (v0.11.0)

역할:
  Basler 산업용 카메라를 비동기 파이프라인에서 "1프레임 grab" 단위로 쉽게
  쓰도록 감싼 얇은 wrapper. StationRunner._run_grab_producer 가 이 클래스의
  grab() 을 반복 호출해 BGR uint8 ndarray 프레임을 받는다.

설계 특징:
  - **안전한 폴백**: pypylon 미설치 또는 카메라 미연결 시 ImportError/장애를
    잡아 is_open=False 로 두고, 호출자가 grab() 결과를 체크해 더미로 전환 가능.
  - **시리얼 번호 선택**: config.ai_server.station*.camera_serial 로
    Station 별 카메라를 안정적으로 식별. 빈 값이면 첫 번째 발견 카메라 사용.
  - **BGR 변환**: OpenCV 호환 포맷(cv2.imwrite 등) 으로 바로 쓰도록 컨버터 내장.
  - **재연결 가능성**: open() 에 실패해도 예외를 던지지 않고 False 반환 —
    런타임에 카메라를 나중에 꽂아도 다음 open() 에서 복구 가능.

사용 예:
    cam = PylonCamera(serial="40012345")
    if cam.open():
        frame = cam.grab(timeout_ms=1000)   # ndarray or None
        cam.close()
    else:
        # 더미 모드로 전환
        ...
"""
from __future__ import annotations

import logging
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# pypylon 는 실제 배포 PC(카메라 연결 PC) 에만 설치된다. CI/개발 환경에서는
# 라이브러리가 없을 수 있으므로 ImportError 를 안전하게 잡아 전역 플래그로 둔다.
# ---------------------------------------------------------------------------
try:
    from pypylon import pylon  # type: ignore
    HAS_PYLON = True
except Exception as _e:   # ImportError 뿐 아니라 OS 레벨 로딩 에러까지 포함
    pylon = None          # 정적 분석용 None 바인딩
    HAS_PYLON = False
    logger.info("pypylon 미탑재 — 카메라 더미 모드로 동작 (사유: %s)", _e)


class PylonCamera:
    """Basler Pylon 카메라의 최소 wrapper."""

    def __init__(self, serial: str = "", input_width: int = 0, input_height: int = 0):
        """카메라 설정만 보관하고 실제 열기는 open() 에서 수행.

        Args:
            serial: 선택할 카메라 시리얼 번호 (빈 값 → 첫 번째 장치).
            input_width / input_height: 출력 크기를 고정하고 싶을 때. 0 이면 카메라 원본 해상도.
        """
        self._serial = serial
        self._input_width = input_width
        self._input_height = input_height

        self._camera = None          # pylon.InstantCamera
        self._converter = None       # pylon.ImageFormatConverter (→ BGR8)
        self._is_open = False

    @property
    def is_open(self) -> bool:
        return self._is_open

    # -----------------------------------------------------------------------
    # open — 장치 열기 + Continuous Grabbing 시작
    # -----------------------------------------------------------------------
    # 반환값: 성공 True / 실패 False (예외는 삼키고 내부 로그만 남긴다)
    def open(self) -> bool:
        if self._is_open:
            return True
        if not HAS_PYLON:
            logger.warning("pypylon 라이브러리가 없어 open() 실패 — 더미 모드 필요")
            return False

        try:
            tl_factory = pylon.TlFactory.GetInstance()
            devices = tl_factory.EnumerateDevices()
            if not devices:
                logger.warning("연결된 Basler 카메라가 없습니다")
                return False

            # 시리얼 지정 시 매칭, 아니면 첫 번째
            target_device = None
            if self._serial:
                for dev in devices:
                    if dev.GetSerialNumber() == self._serial:
                        target_device = dev
                        break
                if target_device is None:
                    logger.warning("시리얼 %s 카메라를 찾지 못함 — 첫 번째 장치로 대체",
                                   self._serial)
                    target_device = devices[0]
            else:
                target_device = devices[0]

            self._camera = pylon.InstantCamera(tl_factory.CreateDevice(target_device))
            self._camera.Open()

            # BGR8 변환기 — 추론기는 OpenCV BGR 기대
            self._converter = pylon.ImageFormatConverter()
            self._converter.OutputPixelFormat = pylon.PixelType_BGR8packed
            self._converter.OutputBitAlignment = pylon.OutputBitAlignment_MsbAligned

            # 지속 촬영 시작 — GrabOne 대신 큐에서 꺼내는 방식으로 FPS 향상
            self._camera.StartGrabbing(pylon.GrabStrategy_LatestImageOnly)

            logger.info("Pylon 카메라 open 성공 | 시리얼=%s 모델=%s",
                        target_device.GetSerialNumber(),
                        target_device.GetModelName())
            self._is_open = True
            return True

        except Exception as exc:
            logger.error("Pylon 카메라 open 실패: %s", exc)
            self._safe_close()
            return False

    # -----------------------------------------------------------------------
    # grab — 1프레임 가져오기 (블로킹, 타임아웃 제한 있음)
    # -----------------------------------------------------------------------
    # 반환값: BGR uint8 ndarray (H, W, 3) 또는 실패 시 None
    def grab(self, timeout_ms: int = 1000) -> Optional[np.ndarray]:
        if not self._is_open or self._camera is None:
            return None

        try:
            grab_result = self._camera.RetrieveResult(
                timeout_ms, pylon.TimeoutHandling_ThrowException
            )
            try:
                if not grab_result.GrabSucceeded():
                    logger.warning("Pylon grab 실패 | code=%d desc=%s",
                                   grab_result.GetErrorCode(),
                                   grab_result.GetErrorDescription())
                    return None
                # 원본 프레임 → BGR8 변환 → ndarray 로 복사 (grab_result 버퍼는 재사용됨)
                image = self._converter.Convert(grab_result)
                arr = image.GetArray()  # H, W, 3 (BGR)
                return arr.copy()
            finally:
                grab_result.Release()
        except Exception as exc:
            logger.error("Pylon grab 예외: %s", exc)
            return None

    # -----------------------------------------------------------------------
    # start_grabbing / stop_grabbing — pause/resume 용 grab 일시중단 API (v0.14.5)
    # -----------------------------------------------------------------------
    # 목적:
    #   INSPECT_CONTROL(pause/resume) 수신 시 grab 루프만 블록하면 카메라 하드웨어는
    #   계속 프레임을 뽑고 있다가 USB/GigE 버퍼에 쌓인다 → resume 순간 오래된 프레임이
    #   한꺼번에 흘러나와 latency 스파이크. 실제 카메라 grabbing 자체를 정지시켜
    #   버퍼에 프레임이 누적되지 않게 한다.
    # 안전:
    #   - 이미 정지/실행 중인 상태면 no-op (IsGrabbing() 체크).
    #   - 실패해도 예외를 던지지 않고 False 반환.
    def start_grabbing(self) -> bool:
        if not self._is_open or self._camera is None or pylon is None:
            return False
        try:
            if not self._camera.IsGrabbing():
                self._camera.StartGrabbing(pylon.GrabStrategy_LatestImageOnly)
                logger.info("Pylon 카메라 grab 재개")
            return True
        except Exception as exc:
            logger.error("Pylon start_grabbing 예외: %s", exc)
            return False

    def stop_grabbing(self) -> bool:
        if not self._is_open or self._camera is None:
            return False
        try:
            if self._camera.IsGrabbing():
                self._camera.StopGrabbing()
                logger.info("Pylon 카메라 grab 정지")
            return True
        except Exception as exc:
            logger.error("Pylon stop_grabbing 예외: %s", exc)
            return False

    # -----------------------------------------------------------------------
    # close — 카메라 종료. 종료 중 예외는 삼킴 (shutdown 경로의 안정성 우선)
    # -----------------------------------------------------------------------
    def close(self) -> None:
        self._safe_close()

    def _safe_close(self) -> None:
        self._is_open = False
        try:
            if self._camera is not None:
                if self._camera.IsGrabbing():
                    self._camera.StopGrabbing()
                if self._camera.IsOpen():
                    self._camera.Close()
        except Exception as exc:
            logger.warning("Pylon 카메라 close 중 예외(무시): %s", exc)
        finally:
            self._camera = None
            self._converter = None
