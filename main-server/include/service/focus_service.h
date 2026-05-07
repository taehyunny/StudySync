#pragma once
// ============================================================================
// focus_service.h — FOCUS_LOG_PUSH 처리
// ============================================================================
// EventBus 의 FOCUS_LOG_PUSH_RECEIVED 를 구독하여:
//   1) FocusLogDao 로 focus_logs 테이블 INSERT
//   2) ACK_SEND_REQUESTED (FOCUS_LOG_ACK 1001) 발행
// 실패 시에도 동일 이벤트로 NACK(ack_ok=false) 발행하여 AI 측 재전송 유도.
// ============================================================================

#include "core/event_bus.h"
#include "storage/dao.h"

namespace factory {

class FocusService {
public:
    FocusService(EventBus& bus, ConnectionPool& pool);
    void register_handlers();

private:
    void on_focus_log_push(const std::any& payload);

    EventBus&    event_bus_;
    FocusLogDao  dao_;
};

} // namespace factory
