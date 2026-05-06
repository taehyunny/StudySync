// ============================================================================
// station_handler.h — 스테이션별 검사 이벤트 핸들러
// ============================================================================
// INSPECTION_INBOUND/ASSEMBLY 이벤트 수신 → InspectionService 호출
// 결과에 따라 ACK/NACK + GUI 푸시 이벤트 발행
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "service/inspection_service.h"

namespace factory {

class Station1Handler {
public:
    Station1Handler(EventBus& bus, InspectionService& service);
    void register_handlers();
private:
    void on_inspection(const std::any& payload);
    EventBus& event_bus_;
    InspectionService& service_;
};

class Station2Handler {
public:
    Station2Handler(EventBus& bus, InspectionService& service);
    void register_handlers();
private:
    void on_inspection(const std::any& payload);
    EventBus& event_bus_;
    InspectionService& service_;
};

} // namespace factory
