#pragma once
// ============================================================================
// ack_sender.h — AI 추론서버 측 ACK 응답 송신기
// ============================================================================
// 도메인 서비스(FocusService / PostureService / TrainHandler) 가 처리 결과를
// AckSendEvent 로 발행하면, 이를 구독하여 송신측 소켓으로 ACK JSON 을 회신.
//
// 패킷 포맷 (변동 없음):
//   [4바이트 BE 길이 헤더] + [JSON 본문]
//
// JSON 본문:
//   { "protocol_no": <int>,
//     "protocol_version": "1.0",
//     "request_id": "<요청 매칭 키>",
//     "ack": <true/false>,
//     "error_message": "<선택>",
//     "image_size": 0 }
// ============================================================================

#include "core/event_bus.h"

#include <cstdint>
#include <string>
#include <vector>

namespace factory {

class AckSender {
public:
    explicit AckSender(EventBus& bus);

    /// ACK_SEND_REQUESTED, MODEL_RELOAD_REQUESTED 구독.
    void register_handlers();

private:
    /// 도메인 서비스 → ACK 송신 요청 핸들러.
    void on_ack_send_requested(const std::any& payload);

    /// 추론서버에 새 모델 바이너리 송신.
    void on_model_reload_requested(const std::any& payload);

    bool send_ack(const std::string& sender_addr,
                  int protocol_no,
                  const std::string& request_id,
                  bool ack_ok,
                  const std::string& error_message);

    bool send_model_reload(const std::string& model_type,
                           const std::string& model_path,
                           const std::string& version,
                           const std::vector<uint8_t>& model_bytes);

    EventBus& event_bus_;
};

} // namespace factory
