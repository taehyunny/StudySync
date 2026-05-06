// ============================================================================
// train_handler.cpp — 학습 완료 이벤트 핸들러 구현
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

    if (result.success) {
        // 학습 서버에 ACK
        AckSendEvent ack{};
        ack.protocol_no   = static_cast<int>(ProtocolNo::TRAIN_COMPLETE_ACK);
        ack.inspection_id = ev.request_id;
        ack.sender_addr   = ev.sender_addr;
        ack.ack_ok        = true;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, ack);

        // 추론서버에 모델 리로드 명령
        if (!ev.model_bytes.empty()) {
            ModelReloadEvent reload{};
            reload.station_id  = ev.station_id;
            reload.model_path  = result.saved_model_path;
            reload.version     = ev.version;
            reload.model_type  = ev.model_type;
            reload.model_bytes = ev.model_bytes;
            event_bus_.publish(EventType::MODEL_RELOAD_REQUESTED, reload);
            log_train("추론서버 모델 리로드 요청 | 스테이션=%d", ev.station_id);
        }

        // GUI 클라이언트에 학습 완료 푸시 (GuiNotifier가 GUI_PUSH_REQUESTED 경로가 아닌
        // TRAIN_COMPLETE_RECEIVED를 직접 구독하므로 별도 발행 불필요 — 이미 구독 중)
    } else {
        log_err_train("학습 처리 실패 | %s", result.error_message.c_str());

        // NACK
        AckSendEvent nack{};
        nack.protocol_no   = static_cast<int>(ProtocolNo::TRAIN_COMPLETE_ACK);
        nack.inspection_id = ev.request_id;
        nack.sender_addr   = ev.sender_addr;
        nack.ack_ok        = false;
        nack.error_message = result.error_message;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, nack);
    }
}

} // namespace factory
