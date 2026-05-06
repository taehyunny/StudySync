"""Protocol.py — TCP 통신 메시지 번호(프로토콜) 정의

이 파일은 AI 서버(추론/학습)와 운용서버(C++ Main Server) 사이에서
TCP로 주고받는 모든 메시지의 번호를 정의한다.

C++ 운용서버의 Protocol.h 파일과 **동일한 번호**를 사용해야
양쪽이 서로의 메시지를 올바르게 인식할 수 있다.

메시지 번호 범위:
  100~199  : 외부 통신 (MFC 클라이언트 ↔ 운용서버)
  1000~1099: 내부 통신 — 추론 서버 관련 (운용 ↔ 추론)
  1100~1199: 내부 통신 — 학습 서버 관련 (운용 ↔ 학습)
  1200~1299: 내부 통신 — 헬스체크 (운용 → 각 서버)
  1300~1399: 내부 통신 — Arduino 제어 (추론 → Arduino)
  1900~1999: 내부 통신 — 공통 ACK/NACK/에러
"""

from __future__ import annotations  # 타입 힌트를 문자열로 처리

from enum import IntEnum  # IntEnum: 정수 값을 가진 열거형 (enum)


# 프로토콜 버전 — 패킷에 항상 포함되어 양쪽 버전 호환을 확인하는 데 사용
PROTOCOL_VERSION = "1.0"


class ProtocolNo(IntEnum):
    """모든 TCP 메시지의 고유 번호를 정의하는 열거형.

    예: ProtocolNo.STATION1_NG → 정수 1000
    패킷을 만들 때 이 번호를 JSON의 "protocol_no" 필드에 넣는다.
    """

    # ===== 외부 100~199: MFC 클라이언트 ↔ 운용서버 =====
    # (AI 서버에서 직접 사용하지 않지만, 번호 예약을 위해 정의)

    LOGIN_REQ              = 100   # MFC → 운용: 로그인 요청
    LOGIN_RES              = 101   # 운용 → MFC: 로그인 응답 (성공/실패)
    LOGOUT_REQ             = 102   # MFC → 운용: 로그아웃 요청
    LOGOUT_RES             = 103   # 운용 → MFC: 로그아웃 응답

    INSPECT_NG_PUSH        = 110   # 운용 → MFC: NG 검사 결과 실시간 푸시
    INSPECT_NG_ACK_EXT     = 111   # MFC → 운용: NG 푸시 수신 확인
    INSPECT_OK_COUNT_PUSH  = 112   # 운용 → MFC: OK/NG 카운트 주기 통계 푸시
    INSPECT_HISTORY_REQ    = 114   # MFC → 운용: 검사 이력 조회 요청
    INSPECT_HISTORY_RES    = 115   # 운용 → MFC: 검사 이력 조회 응답

    STATS_REQ              = 130   # MFC → 운용: 통계 데이터 요청
    STATS_RES              = 131   # 운용 → MFC: 통계 데이터 응답

    MODEL_LIST_REQ         = 150   # MFC → 운용: 모델 목록 조회 요청
    MODEL_LIST_RES         = 151   # 운용 → MFC: 모델 목록 응답
    RETRAIN_REQ            = 152   # MFC → 운용: 재학습 트리거 요청
    RETRAIN_RES            = 153   # 운용 → MFC: 재학습 요청 수락/거부 응답
    RETRAIN_PROGRESS_PUSH  = 154   # 운용 → MFC: 학습 진행 상태 주기 푸시
    MODEL_DEPLOY_NOTIFY    = 156   # 운용 → MFC: 신규 모델 배포 완료 알림
    MODEL_DEPLOY_ACK_EXT   = 157   # MFC → 운용: 배포 알림 수신 확인
    RETRAIN_UPLOAD         = 158   # v0.13.0: MFC → 운용 학습용 이미지 1장 업로드 (JSON+binary)
    RETRAIN_UPLOAD_ACK     = 159   # v0.13.0: 운용 → MFC 업로드 결과 ACK
    INSPECT_CONTROL_REQ    = 160   # v0.14.0: MFC → 운용 검사 일시정지/재개 요청
    INSPECT_CONTROL_RES    = 161   # v0.14.0: 운용 → MFC 결과 ACK

    SERVER_HEALTH_PUSH     = 170   # 운용 → MFC: 각 서버 헬스 상태 푸시 (5초 주기)

    EXT_ACK                = 190   # 외부 통신 범용 ACK (처리 완료)
    EXT_NACK               = 191   # 외부 통신 범용 NACK (처리 실패)
    EXT_ERROR              = 192   # 외부 통신 에러 알림

    # ===== 내부 1000~1099: 추론 서버 ↔ 운용서버 =====
    # (본 AI 서버 코드에서 직접 사용하는 핵심 메시지들)
    #
    # STATION1_NG/STATION2_NG 전송 시 포함되는 바이너리 이미지(순서 고정):
    #   1) 원본 JPEG         — JSON 필드 image_size
    #   2) 원본+히트맵 PNG   — JSON 필드 heatmap_size   (MFC "Anomaly Map" 영역)
    #   3) 원본+PredMask PNG — JSON 필드 pred_mask_size (MFC "Pred Mask" 영역)
    # 크기 필드가 0이면 해당 이미지 생략 (구 버전과 하위호환).

    STATION1_NG            = 1000  # 추론#1 → 운용: 입고검사 NG 결과 (원본+시각화 2장)
    STATION1_NG_ACK        = 1001  # 운용 → 추론#1: NG 수신 확인 (DB 저장 완료)
    STATION2_NG            = 1002  # 추론#2 → 운용: 조립검사 NG 결과 (원본+시각화 2장)
    STATION2_NG_ACK        = 1003  # 운용 → 추론#2: NG 수신 확인
    STATION_OK_COUNT       = 1004  # 추론 → 운용: OK 카운트 주기 보고 (5초마다)
    INSPECT_META           = 1006  # 추론 → 운용: 검사 메타데이터 (OK/NG 공통, DB기록용)

    MODEL_RELOAD_CMD       = 1010  # 운용 → 추론: 새 모델로 교체하라는 명령
    MODEL_RELOAD_RES       = 1011  # 추론 → 운용: 모델 교체 성공/실패 응답
    INFERENCE_CONTROL_CMD  = 1020  # v0.14.0: 운용 → 추론 검사 pause/resume
    INFERENCE_CONTROL_RES  = 1021  # v0.14.0: 추론 → 운용 상태 ACK

    # ===== 내부 1100~1199: 학습 서버 ↔ 운용서버 =====

    TRAIN_START_REQ        = 1100  # 운용 → 학습: 학습 시작 요청
    TRAIN_START_RES        = 1101  # 학습 → 운용: 학습 시작 수락/거부 응답
    TRAIN_PROGRESS         = 1102  # 학습 → 운용: 학습 진행 상태 (epoch, loss, progress%)
    TRAIN_COMPLETE         = 1104  # 학습 → 운용: 학습 완료 (모델 경로, 정확도 포함)
    TRAIN_COMPLETE_ACK     = 1105  # 운용 → 학습: 학습 완료 수신 확인
    TRAIN_FAIL             = 1106  # 학습 → 운용: 학습 실패 알림
    TRAIN_FAIL_ACK         = 1107  # 운용 → 학습: 학습 실패 수신 확인
    TRAIN_DATA_UPLOAD      = 1108  # v0.13.0: 운용 → 학습 학습용 이미지 1장 (JSON+binary)
    TRAIN_DATA_UPLOAD_ACK  = 1109  # v0.13.0: 학습 → 운용 이미지 저장 결과 ACK

    # ===== 내부 1200~1299: 헬스체크 =====

    HEALTH_PING            = 1200  # 운용 → 각 서버: 살아있니? (5초마다 전송)
    HEALTH_PONG            = 1201  # 각 서버 → 운용: 살아있다! (즉시 응답)
    QUEUE_STATUS           = 1210  # 추론 → 운용: 비동기 큐 상태 보고 (모니터링용)
    INFERENCE_TIMEOUT      = 1212  # 추론 → 운용: 추론 타임아웃 알림

    # ===== 내부 1300~1399: Arduino 시리얼 제어 =====

    ARDUINO_REJECT         = 1300  # 추론#1 → Arduino: 리젝트 명령 (서보모터 + LED + 부저)
    ARDUINO_ALERT          = 1302  # 추론#2 → Arduino: 경고 명령 (RGB LED + LCD 불량 표시)

    # ===== 내부 1900~1999: 공통 ACK/NACK/에러 =====

    INTERNAL_ACK           = 1900  # 내부 통신 범용 ACK
    INTERNAL_NACK          = 1901  # 내부 통신 범용 NACK (처리 실패)
    INTERNAL_RETRY_REQ     = 1902  # ACK 미수신 시 재전송 요청
    INTERNAL_RETRY_DATA    = 1903  # 재전송 요청에 대한 데이터 패킷
    INTERNAL_ERROR         = 1904  # 내부 통신 에러 알림


# ── ACK(수신 확인)이 반드시 필요한 메시지 목록 ──
# 이 메시지들은 보낸 후 1초 내에 ACK를 받아야 하고, 못 받으면 최대 3회 재전송한다.
# 데이터 무결성이 중요한 메시지들 (NG 결과, 모델 배포, 학습 완료 등)
ACK_REQUIRED_NOS: frozenset[int] = frozenset({
    ProtocolNo.STATION1_NG,         # 입고검사 NG — 누락되면 불량품이 통과됨
    ProtocolNo.STATION2_NG,         # 조립검사 NG — 누락되면 불량품이 통과됨
    ProtocolNo.MODEL_RELOAD_CMD,    # 모델 교체 명령 — 실패하면 구 모델로 추론됨
    ProtocolNo.TRAIN_COMPLETE,      # 학습 완료 — 누락되면 모델 배포가 안됨
    ProtocolNo.TRAIN_FAIL,          # 학습 실패 — 누락되면 운용서버가 상태를 모름
    ProtocolNo.INSPECT_NG_PUSH,     # MFC에 NG 푸시 — 화면 갱신에 필요
    ProtocolNo.MODEL_DEPLOY_NOTIFY, # 모델 배포 알림 — MFC에서 활성 모델 표시
    # v0.15.0: 학습 데이터 업로드도 ACK 필수로 분류 (C++ requires_ack() 와 일치).
    # AiServer 는 현재 이 두 프로토콜 송신자가 아니지만, 공통 정의로 정합성 유지.
    ProtocolNo.RETRAIN_UPLOAD,      # 클라→메인 (158) — 네트워크 순단 시 재전송
    ProtocolNo.TRAIN_DATA_UPLOAD,   # 메인→학습 (1108) — 학습셋 누락 방지
})


def requires_ack(protocol_no: int) -> bool:
    """주어진 메시지 번호가 ACK를 필요로 하는지 확인한다.

    Args:
        protocol_no: 확인할 메시지 번호 (예: 1000)
    Returns:
        True이면 ACK 필수, False이면 Fire-and-forget(보내고 잊기)
    """
    return protocol_no in ACK_REQUIRED_NOS


# ── NG 메시지 → 기대되는 ACK 번호 매핑 ──
# 예: STATION1_NG(1000)를 보내면 STATION1_NG_ACK(1001)를 기대한다
_ACK_NO_MAP: dict[int, int] = {
    ProtocolNo.STATION1_NG: ProtocolNo.STATION1_NG_ACK,   # 1000 → 1001
    ProtocolNo.STATION2_NG: ProtocolNo.STATION2_NG_ACK,   # 1002 → 1003
}


def expected_ack_no(send_no: int) -> int:
    """보낸 메시지 번호에 대응하는 ACK 번호를 반환한다.

    Args:
        send_no: 보낸 메시지 번호 (예: 1000)
    Returns:
        기대되는 ACK 번호 (예: 1001). 매핑이 없으면 범용 INTERNAL_ACK(1900) 반환.
    """
    return _ACK_NO_MAP.get(send_no, ProtocolNo.INTERNAL_ACK)
