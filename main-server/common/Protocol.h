#pragma once
// ============================================================================
// Protocol.h — StudySync 메인서버 ↔ AI 추론/학습서버 TCP 프로토콜
// ============================================================================
// 정책:
//   - 클라(MFC) ↔ 메인서버 통신은 HTTP REST + JWT 로 이전 (수정정리본.md).
//     따라서 외부 100~199 대역은 폐기하고 enum 에서 제거한다.
//   - 메인서버 ↔ AI 서버는 기존 4바이트 헤더 + JSON(+옵션 바이너리) 골격 유지.
//   - 영상 본체는 메인서버에 저장하지 않으므로 STATION_NG 식 이미지 동봉 메시지는 폐기.
//
// 패킷 구조 (TCP 스트림, 변동 없음):
//   ┌──────────────────┬───────────────┬────────────────────────┐
//   │ 4바이트 길이      │ JSON 페이로드  │ 바이너리 (옵션, 학습 모델용) │
//   │ (big-endian, JSON)│               │ image_size > 0 일 때만   │
//   └──────────────────┴───────────────┴────────────────────────┘
//
// JSON 본문 공통 필드:
//   - protocol_no      : int     (필수, 메시지 번호 — 아래 ProtocolNo enum)
//   - protocol_version : string  (필수, "1.0")
//   - request_id       : string  (요청/응답 매칭 키 — ACK 가 필요한 메시지에서)
//   - timestamp        : string  (ISO8601)
//   - image_size       : int     (모델 바이너리 동봉 시, 바이트 단위)
//
// 번호 대역:
//   1000~1099 : 도메인 메시지 (FOCUS / POSTURE / EVENT / BASELINE)
//   1100~1199 : 학습 채널
//   1200~1299 : 헬스체크
//   1900~1999 : 내부 공통 (ACK/NACK/ERROR)
// ============================================================================

#include <cstdint>

namespace factory {

constexpr std::size_t HEADER_SIZE      = 4;      // JSON 길이 헤더 (big-endian 4바이트)
constexpr uint16_t    MAIN_SERVER_PORT = 9000;   // 메인 서버 AI 측 TCP 리슨 포트
constexpr const char* FACTORY_PROTOCOL_VERSION = "1.0";  // 프로토콜 버전 문자열
                                                          // (이름은 호환을 위해 유지)

// 메시지 번호 enum — JSON "protocol_no" 필드에 정수값 그대로 전송된다.
enum class ProtocolNo : int {
    // ===== StudySync 도메인 1000~1099 =====
    FOCUS_LOG_PUSH         = 1000,  // AI → 메인 (5fps 집중도 분석)
    FOCUS_LOG_ACK          = 1001,  // 메인 → AI
    POSTURE_LOG_PUSH       = 1002,  // AI → 메인 (자세 분석)
    POSTURE_LOG_ACK        = 1003,  // 메인 → AI
    POSTURE_EVENT_PUSH     = 1004,  // AI → 메인 (이벤트 — bad_posture/drowsy/absent/rest_required)
    POSTURE_EVENT_ACK      = 1005,
    BASELINE_CAPTURE_PUSH  = 1006,  // AI → 메인 (기준 자세 캡처 통지, 옵션)
    BASELINE_CAPTURE_ACK   = 1007,

    // 추론 모델 리로드 (학습 완료 후 메인 → 추론서버)
    MODEL_RELOAD_CMD       = 1010,
    MODEL_RELOAD_RES       = 1011,

    // ===== 학습 채널 1100~1199 =====
    TRAIN_START_REQ        = 1100,
    TRAIN_START_RES        = 1101,
    TRAIN_PROGRESS         = 1102,
    TRAIN_COMPLETE         = 1104,
    TRAIN_COMPLETE_ACK     = 1105,
    TRAIN_FAIL             = 1106,
    TRAIN_FAIL_ACK         = 1107,

    // ===== 헬스체크 1200~ =====
    HEALTH_PING            = 1200,
    HEALTH_PONG            = 1201,
    QUEUE_STATUS           = 1210,

    // ===== 내부 공통 1900~ =====
    INTERNAL_ACK           = 1900,
    INTERNAL_NACK          = 1901,
    INTERNAL_ERROR         = 1904,
};

// 해당 프로토콜 번호가 ACK 응답을 필수로 요구하는지 판정.
// ACK 가 필요한 메시지는 타임아웃 내 응답이 없으면 AI 측에서 재전송 대상이 됨.
inline bool requires_ack(ProtocolNo no) {
    switch (no) {
        case ProtocolNo::FOCUS_LOG_PUSH:
        case ProtocolNo::POSTURE_LOG_PUSH:
        case ProtocolNo::POSTURE_EVENT_PUSH:
        case ProtocolNo::BASELINE_CAPTURE_PUSH:
        case ProtocolNo::MODEL_RELOAD_CMD:
        case ProtocolNo::TRAIN_COMPLETE:
        case ProtocolNo::TRAIN_FAIL:
            return true;
        default:
            return false;
    }
}

// 푸시 메시지 → 대응하는 ACK 메시지 매핑.
// 매핑되지 않는 번호는 INTERNAL_ACK 로 대체.
inline ProtocolNo ack_no_for(ProtocolNo push_no) {
    switch (push_no) {
        case ProtocolNo::FOCUS_LOG_PUSH:        return ProtocolNo::FOCUS_LOG_ACK;
        case ProtocolNo::POSTURE_LOG_PUSH:      return ProtocolNo::POSTURE_LOG_ACK;
        case ProtocolNo::POSTURE_EVENT_PUSH:    return ProtocolNo::POSTURE_EVENT_ACK;
        case ProtocolNo::BASELINE_CAPTURE_PUSH: return ProtocolNo::BASELINE_CAPTURE_ACK;
        case ProtocolNo::TRAIN_COMPLETE:        return ProtocolNo::TRAIN_COMPLETE_ACK;
        case ProtocolNo::TRAIN_FAIL:            return ProtocolNo::TRAIN_FAIL_ACK;
        default:                                return ProtocolNo::INTERNAL_ACK;
    }
}

} // namespace factory
