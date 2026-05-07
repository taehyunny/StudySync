#pragma once
// ============================================================================
// focus_service.h — FOCUS_LOG_PUSH 처리 (현재 미사용 / dead path)
// ============================================================================
// 옛 옵션 B (AI → 메인 TCP push) 흐름의 잔재. 클라가 직접 메인 HTTP `/log/ingest`
// 로 NDJSON 보내는 옵션 A 로 정착되어 이 서비스로의 인입은 발생하지 않음.
//
// 코드는 유지 — Protocol.h 1000 (FOCUS_LOG_PUSH) enum + 라우터 dispatch 가
// 살아 있어 현재 통합 환경에서 dead 지만, 향후 AI 가 직접 푸시 모드로 돌아가면
// 즉시 재활성화 가능. 제거하려면 Protocol.h / router / event_types.h 도 동시 정리.
//
// 활성 시 동작:
//   1) FocusLogDao 로 focus_logs 테이블 INSERT
//   2) ACK_SEND_REQUESTED (FOCUS_LOG_ACK 1001) 발행
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
