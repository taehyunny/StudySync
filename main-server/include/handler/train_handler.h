// ============================================================================
// train_handler.h — 학습 완료/실패 이벤트 핸들러
// ============================================================================
// TRAIN_COMPLETE_RECEIVED → TrainService 호출 → ACK + MODEL_RELOAD 발행
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "service/train_service.h"

namespace factory {

class TrainHandler {
public:
    TrainHandler(EventBus& bus, TrainService& service);
    void register_handlers();

private:
    void on_train_complete(const std::any& payload);

    EventBus&     event_bus_;
    TrainService& train_service_;
};

} // namespace factory
