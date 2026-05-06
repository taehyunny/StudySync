// ============================================================================
// station_handler.cpp — 검사 이벤트 → 검증 → 즉시 ACK → 백그라운드 영속화
// ============================================================================
// v0.12.0 변경:
//   이전: process() 동기 호출로 이미지 저장 + DB INSERT 가 끝날 때까지 블로킹
//         → AI 서버 ACK 타임아웃(1~5초) 유발
//   이후: validate_only() 로 빠른 검증(<1ms) → 즉시 ACK 발행
//         → INSPECTION_VALIDATED 이벤트로 백그라운드 워커에 persist 위임
//
// 흐름:
//   INSPECTION_INBOUND  ─► Station1Handler ─► validate_only ─► ACK + VALIDATED
//   INSPECTION_ASSEMBLY ─► Station2Handler ─► validate_only ─► ACK + VALIDATED
//                                                                    │
//                                     (EventBus 별도 워커 스레드)    ▼
//                             InspectionService::on_validated ──► persist
//                                                                    │
//                                                  GUI_PUSH_REQUESTED ▼
// ============================================================================
#include "handler/station_handler.h"
#include "core/logger.h"
#include "Protocol.h"

namespace factory {

// 공통 유틸 — validate 후 ACK/NACK + INSPECTION_VALIDATED 발행
// Station1/2 핸들러의 로직이 동일하므로 한 곳에 모은다.
static void handle_inspection_common(EventBus& bus,
                                     InspectionService& service,
                                     InspectionEvent ev,          // 복사본: 정규화 부수효과 수용
                                     const char* station_label)
{
    log_ai("%s NG 수신 | 점수=%.2f 결함=%s",
           station_label, ev.score, ev.defect_type.c_str());

    std::string err;
    const bool ok = service.validate_only(ev, err);

    AckSendEvent ack{};
    ack.protocol_no   = static_cast<int>(
        ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
    ack.inspection_id = ev.inspection_id;
    ack.sender_addr   = ev.sender_addr;
    ack.ack_ok        = ok;
    if (!ok) ack.error_message = err;

    // ① ACK/NACK 즉시 발행 — AI 서버 타임아웃 방지 (validate_only 는 <1ms)
    bus.publish(EventType::ACK_SEND_REQUESTED, ack);

    // ② 검증 통과 → 백그라운드 영속화 트리거 (이미지 저장 + DB + GUI 푸시)
    if (ok) {
        bus.publish(EventType::INSPECTION_VALIDATED, ev);
    } else {
        log_err_ai("검증 실패 | %s id=%s", err.c_str(), ev.inspection_id.c_str());
    }
}

// ===== Station1Handler (입고검사) =============================================

Station1Handler::Station1Handler(EventBus& bus, InspectionService& service)
    : event_bus_(bus), service_(service) {
}

void Station1Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_INBOUND,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station1Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    handle_inspection_common(event_bus_, service_, ev, "입고검사");
}

// ===== Station2Handler (조립검사) =============================================

Station2Handler::Station2Handler(EventBus& bus, InspectionService& service)
    : event_bus_(bus), service_(service) {
}

void Station2Handler::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_ASSEMBLY,
                         [this](const std::any& p) { this->on_inspection(p); });
}

void Station2Handler::on_inspection(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    handle_inspection_common(event_bus_, service_, ev, "조립검사");
}

} // namespace factory
