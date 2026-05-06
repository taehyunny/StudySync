"""SerialCtrl.py — Arduino 시리얼 통신 제어

역할:
  AI 추론서버가 NG(불량) 판정을 내렸을 때 Arduino 에 시리얼(USB) 명령을 보내
  물리적 동작(리젝트 서보/LED/부저/LCD 알림) 을 트리거한다.

명령 프로토콜 (줄바꿈으로 구분된 평문):
  Station1 (입고검사)  "REJECT:결함유형\\n"      → 서보모터 리젝트 + 빨간 LED + 부저
  Station2 (조립검사)  "ALERT:결함목록\\n"       → RGB LED + LCD 에 불량 유형 표시
  아두이노 스케치는 Arduino/arduino_led_control/ 참조.

설계 특징:
  - pyserial 미설치/포트 미연결 시에도 예외를 던지지 않고 "no-op" 로 동작.
    개발/CI 환경에서 아두이노 없이 검사 파이프라인을 테스트할 수 있도록 함.
  - port=None 이면 "Arduino 미사용" 으로 간주하고 open()/send_command() 가 모두 조용히 패스.

의존성:
  실제 사용 시 pyserial 필요 → `pip install pyserial`
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

import logging  # 로그 출력용 표준 라이브러리
from typing import Optional  # Optional: 값이 None일 수 있음


# 이 모듈 전용 로거 생성 — 로그 메시지에 모듈 이름이 표시된다
logger = logging.getLogger(__name__)


class SerialCtrl:
    """Arduino 시리얼 명령 송신기.

    사용 흐름:
        1. SerialCtrl("COM3", 9600) — 객체 생성 (아직 연결 안 됨)
        2. .open() — 시리얼 포트 열기
        3. .send_command("REJECT:crack\n") — 명령 전송
        4. .close() — 시리얼 포트 닫기
    """

    def __init__(self, port: Optional[str], baud: int = 9600):
        """시리얼 컨트롤러 초기화.

        Args:
            port: 시리얼 포트 이름 (예: "COM3", "/dev/ttyUSB0")
                  None이면 Arduino를 사용하지 않는 것으로 간주.
            baud: 통신 속도 (기본 9600bps, Arduino 기본값과 일치)
        """
        self._port = port            # 시리얼 포트 이름
        self._baud = baud            # 통신 속도 (baud rate)
        self._serial = None          # pyserial.Serial 인스턴스 (open() 시 생성)
        # v0.14.8: 미연결 상태에서 send_command 가 무한히 debug 로그만 찍는 걸 방지.
        # 첫 전송 시도 시 한 번만 WARNING 으로 명시적으로 알려준다.
        self._warned_noop = False

    def open(self) -> None:
        """시리얼 포트를 연다.

        port가 None이면 아무 것도 하지 않는다 (Arduino 미사용 모드).
        실제 환경에서는 pyserial의 Serial 객체를 생성한다.
        """
        if self._port is None:
            # 포트가 설정되지 않았으면 Arduino를 사용하지 않는다
            logger.info("SerialCtrl: port not configured, skip open")
            return
        try:
            import serial  # pyserial 패키지 — pip install pyserial

            # 시리얼 포트 열기: timeout=1초 (응답 대기 최대 1초)
            self._serial = serial.Serial(self._port, self._baud, timeout=1)
            logger.info("SerialCtrl opened %s @ %d bps", self._port, self._baud)
        except ImportError:
            # pyserial이 설치되지 않은 환경 (개발/테스트 시)
            logger.warning("pyserial not installed — SerialCtrl in dummy mode")
        except Exception as exc:
            # 포트 열기 실패 (포트 없음, 권한 문제 등)
            logger.error("SerialCtrl open failed: %s", exc)

    def close(self) -> None:
        """시리얼 포트를 닫는다. 이미 닫혀있으면 아무 것도 안 한다."""
        if self._serial is not None:
            try:
                self._serial.close()  # 포트 닫기
            except Exception:
                pass  # 닫기 실패해도 무시 (이미 닫혀있을 수 있음)
            self._serial = None  # 참조 해제

    def send_command(self, command: str) -> None:
        """Arduino에 단일 명령을 전송한다.

        Args:
            command: 전송할 명령 문자열
                Station1 예: "REJECT:crack\n"
                Station2 예: "ALERT:cap_missing,label_tilt\n"

        시리얼이 연결되지 않은 상태면 로그만 출력하고 넘어간다.
        """
        if self._serial is None:
            # v0.14.8: 첫 호출 시 한 번만 WARNING — 이후는 DEBUG (로그 노이즈 방지).
            # 운영자가 "왜 Arduino 가 반응 안 하지?" 를 빠르게 파악할 수 있게 한다.
            if not self._warned_noop:
                logger.warning(
                    "SerialCtrl 명령 무시 (포트 미연결) | port=%s — config.json 의 "
                    "arduino_port 를 설정하거나 Arduino 연결 상태를 확인하세요. "
                    "명령: %s", self._port, command.strip()
                )
                self._warned_noop = True
            else:
                logger.debug("SerialCtrl noop send: %s", command.strip())
            return
        try:
            # 문자열을 ASCII 바이트로 변환하여 전송
            self._serial.write(command.encode("ascii"))
            logger.info("SerialCtrl 송신 | %s", command.strip())
        except Exception as exc:
            logger.error("SerialCtrl send failed: %s", exc)
