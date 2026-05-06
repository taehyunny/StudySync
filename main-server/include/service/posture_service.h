#pragma once
// ============================================================================
// posture_service.h — 자세 도메인 처리 (Log / Event / Baseline)
// ============================================================================
// 구독 이벤트:
//   POSTURE_LOG_PUSH_RECEIVED    → posture_logs INSERT + ACK
//   POSTURE_EVENT_PUSH_RECEIVED  → posture_events 멱등 INSERT + ACK
//   BASELINE_CAPTURE_RECEIVED    → 로깅 + ACK (저장 위치 미결정 — 수정정리본.md §5)
//
// 영상 본체는 메인서버에 저장하지 않음. clip_* 메타데이터만 DB 에 보관.
// ============================================================================

#include "core/event_bus.h"
#include "storage/dao.h"

namespace factory {

class PostureService {
public:
    PostureService(EventBus& bus, ConnectionPool& pool);
    void register_handlers();

private:
    void on_posture_log_push(const std::any& payload);
    void on_posture_event_push(const std::any& payload);
    void on_baseline_capture(const std::any& payload);

    EventBus&        event_bus_;
    PostureLogDao    log_dao_;
    PostureEventDao  event_dao_;
};

} // namespace factory
