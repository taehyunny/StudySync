#pragma once
// ============================================================================
// Protocol.h — 공장 품질관리 시스템 통신 프로토콜 정의
// ============================================================================
// AI 추론 서버 <-> 메인 운영 서버 간 바이너리+JSON 프로토콜을 정의한다.
//
// 패킷 구조 (TCP 스트림):
//   ┌──────────────────┬──────────────┬──────────────────────┐
//   │ 4바이트 길이      │ JSON 페이로드 │ 이미지 바이너리 (선택) │
//   │ (big-endian,      │              │ image_size > 0일 때   │
//   │  JSON 크기만)     │              │ JSON 뒤에 이어붙임    │
//   └──────────────────┴──────────────┴──────────────────────┘
//
// JSON 본문 공통 필드 (요구사항 분석서 "공통 패킷 구조" 기준):
//   - protocol_no      : int     (필수, 메시지 번호 — 아래 ProtocolNo enum)
//   - protocol_version : string  (필수, "1.0")
//   - inspection_id    : string  (검사 결과 계열에서 필수)
//   - request_id       : string  (요청/응답 매칭용, optional)
//   - station_id       : int     (1=입고, 2=조립)
//   - timestamp        : string  (ISO8601)
//   - image_size       : int     (NG 이미지 동봉 시, 바이트 단위)
//
// 번호 대역:
//   외부(MFC 클라이언트 ↔ 운영서버): 100~199
//   내부(운영서버 ↔ 추론/학습서버):  1000~1999
// ============================================================================

#include <cstdint>

namespace factory {

constexpr std::size_t HEADER_SIZE      = 4;      // JSON 길이 헤더 크기 (big-endian 4바이트)
constexpr uint16_t    MAIN_SERVER_PORT = 9000;    // 메인 운영서버 TCP 리슨 포트
constexpr const char* FACTORY_PROTOCOL_VERSION = "1.0";  // 프로토콜 버전 문자열

// 스테이션(검사 공정) 식별자
enum class StationId : int {
    INBOUND  = 1,   // Station1: 입고 검사 (양품/불량 분류)
    ASSEMBLY = 2,   // Station2: 조립 검사 (캡/라벨/충전 상태 + 이상 탐지)
};

// 메시지 번호 enum — JSON의 "protocol_no" 필드에 정수값 그대로 전송된다.
// 홀수는 응답/ACK, 짝수는 요청/데이터 전송이 일반적인 관례.
enum class ProtocolNo : int {
    // ===== 외부 100~199 (MFC 클라이언트 ↔ 운영서버) — 추후 구현, 번호만 예약 =====
    LOGIN_REQ              = 100,
    LOGIN_RES              = 101,
    LOGOUT_REQ             = 102,
    LOGOUT_RES             = 103,
    REGISTER_REQ           = 104,
    REGISTER_RES           = 105,
    INSPECT_NG_PUSH        = 110,
    INSPECT_NG_ACK_EXT     = 111,
    INSPECT_OK_COUNT_PUSH  = 112,
    INSPECT_HISTORY_REQ    = 114,
    INSPECT_HISTORY_RES    = 115,
    INSPECT_IMAGE_REQ      = 116,   // 이력 항목의 이미지 3장 on-demand 요청 (v0.10+)
    INSPECT_IMAGE_RES      = 117,   // 이미지 응답: JSON 뒤에 [원본][히트맵][마스크] 바이너리
    STATS_REQ              = 130,
    STATS_RES              = 131,
    MODEL_LIST_REQ         = 150,
    MODEL_LIST_RES         = 151,
    RETRAIN_REQ            = 152,
    RETRAIN_RES            = 153,
    RETRAIN_PROGRESS_PUSH  = 154,
    MODEL_DEPLOY_NOTIFY    = 156,
    MODEL_DEPLOY_ACK_EXT   = 157,
    RETRAIN_UPLOAD         = 158,   // v0.13.0: 클라→메인 학습용 이미지 1장 업로드 (JSON+binary)
    RETRAIN_UPLOAD_ACK     = 159,   // v0.13.0: 메인→클라 업로드 결과 ACK
    INSPECT_CONTROL_REQ    = 160,   // v0.14.0: 클라→메인 검사 일시정지/재개 요청 ("pause"|"resume")
    INSPECT_CONTROL_RES    = 161,   // v0.14.0: 메인→클라 결과 ACK
    SERVER_HEALTH_PUSH     = 170,
    EXT_ACK                = 190,
    EXT_NACK               = 191,
    EXT_ERROR              = 192,

    // ===== 내부 1000~1999 (운용 ↔ 추론/학습) — 본 단계 구현 대상 =====
    STATION1_NG            = 1000,  // 추론#1 → 운용 (ACK 필수)
    STATION1_NG_ACK        = 1001,  // 운용 → 추론#1
    STATION2_NG            = 1002,  // 추론#2 → 운용 (ACK 필수)
    STATION2_NG_ACK        = 1003,  // 운용 → 추론#2
    STATION_OK_COUNT       = 1004,  // 추론 → 운용 (손실 허용)
    INSPECT_META           = 1006,  // 추론 → 운용 (DB 기록용, ACK 미사용)

    MODEL_RELOAD_CMD       = 1010,
    MODEL_RELOAD_RES       = 1011,
    INFERENCE_CONTROL_CMD  = 1020,  // v0.14.0: 운용 → 추론 검사 일시정지/재개 명령
    INFERENCE_CONTROL_RES  = 1021,  // v0.14.0: 추론 → 운용 상태 ACK

    // 학습서버 채널 1100~1199 — 번호만 예약
    TRAIN_START_REQ        = 1100,
    TRAIN_START_RES        = 1101,
    TRAIN_PROGRESS         = 1102,
    TRAIN_COMPLETE         = 1104,
    TRAIN_COMPLETE_ACK     = 1105,
    TRAIN_FAIL             = 1106,
    TRAIN_FAIL_ACK         = 1107,
    TRAIN_DATA_UPLOAD      = 1108,  // v0.13.0: 메인→학습 학습용 이미지 1장 (JSON+binary)
    TRAIN_DATA_UPLOAD_ACK  = 1109,  // v0.13.0: 학습→메인 업로드 결과 ACK

    // 헬스체크 1200~
    HEALTH_PING            = 1200,
    HEALTH_PONG            = 1201,
    QUEUE_STATUS           = 1210,
    INFERENCE_TIMEOUT      = 1212,

    // Arduino 1300~ (Edge ↔ Arduino, 본 서버 미사용)
    ARDUINO_REJECT         = 1300,
    ARDUINO_ALERT          = 1302,

    // 내부 공통 1900~
    INTERNAL_ACK           = 1900,
    INTERNAL_NACK          = 1901,
    INTERNAL_RETRY_REQ     = 1902,
    INTERNAL_RETRY_DATA    = 1903,
    INTERNAL_ERROR         = 1904,
};

// 해당 프로토콜 번호가 ACK 응답을 필수로 요구하는지 판정한다.
// ACK가 필요한 메시지는 타임아웃 내 응답이 없으면 재전송 대상이 된다.
inline bool requires_ack(ProtocolNo no) {
    switch (no) {
        case ProtocolNo::STATION1_NG:
        case ProtocolNo::STATION2_NG:
        case ProtocolNo::MODEL_RELOAD_CMD:
        case ProtocolNo::TRAIN_COMPLETE:
        case ProtocolNo::TRAIN_FAIL:
        case ProtocolNo::INSPECT_NG_PUSH:
        case ProtocolNo::MODEL_DEPLOY_NOTIFY:
        // v0.15.0: 학습 데이터 업로드 파이프라인 ACK 필수화.
        //   v0.13.0 도입 당시 requires_ack 누락 → 네트워크 순단 시 재전송 미적용.
        case ProtocolNo::RETRAIN_UPLOAD:         // 클라→메인 (158)
        case ProtocolNo::TRAIN_DATA_UPLOAD:      // 메인→학습 (1108)
            return true;
        default:
            return false;
    }
}

// NG(불량) 데이터 패킷 번호로부터 대응하는 ACK 패킷 번호를 반환한다.
// 매핑되지 않는 번호는 범용 INTERNAL_ACK로 대체한다.
inline ProtocolNo ack_no_for(ProtocolNo ng_no) {
    switch (ng_no) {
        case ProtocolNo::STATION1_NG: return ProtocolNo::STATION1_NG_ACK;
        case ProtocolNo::STATION2_NG: return ProtocolNo::STATION2_NG_ACK;
        default:                      return ProtocolNo::INTERNAL_ACK;
    }
}

} // namespace factory
