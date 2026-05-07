// ============================================================================
// train_handler.cpp — 학습 완료 이벤트 핸들러
// ============================================================================
// 흐름:
//   TRAIN_COMPLETE_RECEIVED ─[TrainService.process]─> ACK + MODEL_RELOAD 발행
// ============================================================================
#include "handler/train_handler.h"
#include "core/logger.h"
#include "Protocol.h"

namespace factory {

TrainHandler::TrainHandler(EventBus& bus, TrainService& service)
    : event_bus_(bus), train_service_(service) {
}

void TrainHandler::register_handlers() {
    event_bus_.subscribe(EventType::TRAIN_COMPLETE_RECEIVED,
                         [this](const std::any& p) { this->on_train_complete(p); });
}

void TrainHandler::on_train_complete(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainCompleteEvent&>(payload);

    log_train("학습 완료 수신 | 모델=%s 버전=%s 정확도=%.4f 파일=%zu bytes",
              ev.model_type.c_str(), ev.version.c_str(), ev.accuracy,
              ev.model_bytes.size());

    auto result = train_service_.process(ev);

    AckSendEvent ack{};
    ack.protocol_no = static_cast<int>(ProtocolNo::TRAIN_COMPLETE_ACK);
    ack.request_id  = ev.request_id;
    ack.sender_addr = ev.sender_addr;
    ack.ack_ok      = result.success;
    if (!result.success) ack.error_message = result.error_message;
    if (!event_bus_.publish_critical(EventType::ACK_SEND_REQUESTED, ack,
                                     std::chrono::milliseconds(200), true)) {
        log_err_ack("TRAIN_COMPLETE_ACK 큐잉 실패 | req=%s", ack.request_id.c_str());
    }

    if (result.success && !ev.model_bytes.empty()) {
        ModelReloadEvent reload{};
        reload.model_type  = ev.model_type;
        reload.model_path  = result.saved_model_path;
        reload.version     = ev.version;
        reload.model_bytes = ev.model_bytes;
        if (!event_bus_.publish_critical(EventType::MODEL_RELOAD_REQUESTED, reload)) {
            log_err_train("MODEL_RELOAD_REQUEST 큐잉 실패 | type=%s", ev.model_type.c_str());
        }
        log_train("추론서버 모델 리로드 요청 | type=%s", ev.model_type.c_str());
    }
}

} // namespace factory
