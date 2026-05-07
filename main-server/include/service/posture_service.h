#pragma once
// ============================================================================
// posture_service.h — 자세 도메인 처리 (현재 미사용 / dead path)
// ============================================================================
// 옛 옵션 B (AI → 메인 TCP push) 흐름의 잔재. 클라 직접 HTTP `/log/ingest` 로
// 정착되어 이 서비스로의 인입은 발생하지 않음. 코드 유지 — 즉시 재활성화 가능.
//
// 영상 본체는 메인서버에 저장하지 않음. clip_* 메타데이터만 DB 에 보관 (Claim Check).
//
// 활성 시 구독:
//   POSTURE_LOG_PUSH_RECEIVED    → posture_logs INSERT + ACK
//   POSTURE_EVENT_PUSH_RECEIVED  → posture_events 멱등 INSERT + ACK
//   BASELINE_CAPTURE_RECEIVED    → 로깅 + ACK (저장 위치 미결정)
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
