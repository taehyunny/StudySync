"""ConfigLoader.py — config/config.json 통합 설정 로더

이 파일은 프로젝트 루트의 config/config.json을 읽어와
AiServer 전체에서 공유하기 위한 유틸리티 클래스이다.

핵심 개념:
  - 싱글톤(Singleton): 한 번 로드한 설정을 전체 프로그램에서 공유.
  - 점(.) 구분 키: "network.main_server_host" 같은 중첩 키로 값 조회.

사용법:
    from Common.ConfigLoader import ConfigLoader
    ConfigLoader.load()  # 기본 경로 또는 CONFIG_PATH 환경변수에서 로드
    host = ConfigLoader.get("network.main_server_host")
    port = ConfigLoader.get_int("network.main_server_ai_port")

우선순위:
    명령줄 인자 > CONFIG_PATH 환경변수 > 기본값(../config/config.json)
"""

# __future__.annotations: 타입 힌트를 문자열로 지연 평가.
# Python 3.10 미만 호환을 위해 사용.
from __future__ import annotations

# json: JSON 파일을 읽고/쓰기 위한 표준 라이브러리
import json

# logging: 로그 메시지 기록을 위한 표준 라이브러리
# print 대신 logger를 쓰면 로그 수준(INFO/WARNING/ERROR)별 필터링이 가능.
import logging

# os: 운영체제와 상호작용 (환경변수 읽기 등)
import os

# sys: 파이썬 인터프리터 관련 기능 (현재 파일에서는 사용 안 하지만 대비용)
import sys

# Path: 객체지향 파일 경로 (문자열 + 연산자보다 안전하고 편리)
from pathlib import Path

# Any: 어떤 타입이든 가능한 값, Optional: None일 수도 있는 값
from typing import Any, Optional

# 이 모듈 전용 로거 생성 (로그에 모듈 이름이 자동으로 표시됨)
logger = logging.getLogger(__name__)


class ConfigLoader:
    """통합 설정 로더 (클래스 메서드 기반 싱글톤).

    모든 메서드가 @classmethod이므로 인스턴스를 만들 필요 없이 바로 사용 가능.
    _config와 _path는 클래스 변수로 한 번 로드 후 모든 곳에서 공유된다.
    """

    # 클래스 변수: 모든 인스턴스(및 클래스 메서드)가 공유하는 변수
    _config: Optional[dict] = None  # 로드된 설정 전체 (JSON 딕셔너리)
    _path: Optional[Path] = None    # 로드한 config.json 파일 경로 (디버깅용)

    @classmethod
    def load(cls, path: Optional[str] = None) -> None:
        """config.json 파일을 로드하여 메모리에 저장한다.

        용도:
          - 프로그램 시작 시 한 번만 호출하여 설정을 로드.
          - 이후에는 get/get_int/get_float 등으로 값을 조회.

        매개변수:
          path (Optional[str]): 명시적 파일 경로.
            None이면 환경변수 CONFIG_PATH를 확인하고,
            그것도 없으면 기본 경로들을 순서대로 탐색.

        반환값: 없음 (None). 실패 시 FileNotFoundError 발생.
        """
        # 실제로 사용할 경로를 담을 변수 (None으로 초기화)
        resolved: Optional[Path] = None

        # ── 경로 결정 로직 (우선순위) ──
        if path:
            # 1순위: 함수 인자로 명시된 경로
            resolved = Path(path)
        elif env := os.getenv("CONFIG_PATH"):
            # 2순위: 환경변수 CONFIG_PATH
            # := 왈러스 연산자: 값을 변수에 할당하면서 동시에 조건 검사 (Python 3.8+)
            resolved = Path(env)
        else:
            # 3순위: 기본 경로들을 순서대로 탐색
            # __file__: 현재 파일(ConfigLoader.py)의 경로
            # resolve(): 절대 경로로 변환
            here = Path(__file__).resolve()
            # 3가지 후보 경로를 정의 (가능성 높은 순서)
            # Factory/AiServer/Common/ConfigLoader.py 기준:
            #   parent.parent.parent = Factory/
            candidates = [
                here.parent.parent.parent / "config" / "config.json",  # 프로젝트 루트 기준
                Path.cwd() / "config" / "config.json",                  # 현재 작업 디렉토리 기준
                Path("../config/config.json"),                           # 상대 경로
            ]
            # 각 후보 경로를 순회하며 파일 존재 여부 확인
            for c in candidates:
                if c.exists():
                    resolved = c  # 존재하는 첫 번째 경로 사용
                    break

        # 파일을 찾지 못했거나, 경로가 있어도 파일이 없으면 에러 발생
        if not resolved or not resolved.exists():
            logger.error("config.json을 찾을 수 없습니다")
            raise FileNotFoundError("config.json not found")

        # 파일을 UTF-8로 열어서 JSON 파싱
        # with 구문: 파일을 자동으로 닫아줌 (try-finally 자동 처리)
        with open(resolved, "r", encoding="utf-8") as f:
            cls._config = json.load(f)  # JSON → dict로 변환하여 저장

        # 로드된 파일 경로도 저장 (나중에 source_path()로 조회 가능)
        cls._path = resolved
        # 성공 로그 출력 (어느 경로에서 로드했는지 확인 가능)
        logger.info("config 로드 완료: %s", resolved)

    @classmethod
    def _get(cls, key: str, default: Any = None) -> Any:
        """내부 헬퍼: 점(.) 구분 키로 중첩된 값을 조회한다.

        예: "network.main_server_host" → _config["network"]["main_server_host"]

        매개변수:
          key (str): 점으로 구분된 키 경로
          default (Any): 키를 찾지 못했을 때 반환할 기본값

        반환값:
          Any: 조회된 값 또는 default
        """
        # 로드가 안 됐으면 에러 (load() 호출 필수)
        if cls._config is None:
            raise RuntimeError("ConfigLoader.load()를 먼저 호출하세요")

        # 현재 위치를 config 루트로 초기화
        value: Any = cls._config

        # 점으로 분리된 각 키를 순차적으로 따라가며 값을 탐색
        for k in key.split("."):
            # 현재 위치가 dict이고 해당 키가 있으면 내려간다
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                # 키가 없으면 기본값 반환 (중첩 경로 중간에 빠진 경우도 포함)
                return default
        return value

    @classmethod
    def get(cls, key: str, default: str = "") -> str:
        """문자열 값을 조회한다.

        매개변수:
          key (str): 점 구분 키
          default (str): 기본값 (키 없을 때 반환)

        반환값:
          str: 조회된 값 또는 default
        """
        v = cls._get(key, default)
        # 값이 None이면 default 반환, 아니면 문자열로 변환하여 반환
        return str(v) if v is not None else default

    @classmethod
    def get_int(cls, key: str, default: int = 0) -> int:
        """정수 값을 조회한다.

        매개변수:
          key (str): 점 구분 키
          default (int): 기본값

        반환값:
          int: 조회된 값(정수 변환) 또는 default
        """
        v = cls._get(key, default)
        try:
            return int(v)  # 문자열도 정수로 변환 시도
        except (TypeError, ValueError):
            # 변환 실패 시 기본값 반환 (None, "abc" 등 변환 불가한 값)
            return default

    @classmethod
    def get_float(cls, key: str, default: float = 0.0) -> float:
        """실수 값을 조회한다.

        매개변수:
          key (str): 점 구분 키
          default (float): 기본값

        반환값:
          float: 조회된 값(실수 변환) 또는 default
        """
        v = cls._get(key, default)
        try:
            return float(v)
        except (TypeError, ValueError):
            return default

    @classmethod
    def get_bool(cls, key: str, default: bool = False) -> bool:
        """불리언(True/False) 값을 조회한다.

        JSON에서 true/false는 bool 타입으로 자동 파싱되지만,
        문자열 "true"/"1"/"yes"도 True로 해석한다.

        매개변수:
          key (str): 점 구분 키
          default (bool): 기본값

        반환값:
          bool: 조회된 값 또는 default
        """
        v = cls._get(key, default)
        # 이미 bool 타입이면 그대로 반환
        if isinstance(v, bool):
            return v
        # 문자열이면 특정 문자열("true", "1", "yes")만 True로 변환
        if isinstance(v, str):
            return v.lower() in ("true", "1", "yes")
        # 그 외 타입(숫자, None 등)은 기본값 사용
        return default

    @classmethod
    def get_list(cls, key: str) -> list:
        """리스트 값을 조회한다 (배열).

        매개변수:
          key (str): 점 구분 키

        반환값:
          list: 조회된 리스트, 없거나 리스트가 아니면 빈 리스트
        """
        v = cls._get(key, [])
        # 실제 list 타입일 때만 반환, 아니면 빈 리스트
        return v if isinstance(v, list) else []

    @classmethod
    def source_path(cls) -> Optional[Path]:
        """로드된 config.json 파일의 경로를 반환한다 (디버깅용).

        반환값:
          Optional[Path]: 로드한 파일 경로, 아직 load() 안 했으면 None
        """
        return cls._path
