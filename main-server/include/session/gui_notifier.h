// ============================================================================
// gui_notifier.h — GUI 푸시 알림 브릿지
// ============================================================================
// 목적:
//   EventBus에서 발생하는 품질검사·서버상태·학습진행 이벤트를 구독하여,
//   SessionManager를 통해 연결된 MFC 클라이언트에 실시간 JSON 푸시를 수행한다.
//
// 구독 이벤트 → 프로토콜 매핑:
//   GUI_PUSH_REQUESTED     → protocol 110 (NG 검출 푸시)
//   SERVER_DOWN/RECOVERED  → protocol 170 (서버 장애/복구 알림)
//   OK_COUNT_RECEIVED      → protocol 112 (양품/불량 카운트 갱신)
//   TRAIN_PROGRESS/COMPLETE/FAIL → protocol 154 (재학습 진행·완료·실패)
// ============================================================================
#pragma once

#include "core/event_bus.h"

namespace factory {

class GuiNotifier {
public:
    explicit GuiNotifier(EventBus& bus);
    void register_handlers();

private:
    void on_gui_push(const std::any& payload);
    void on_server_status(const std::any& payload, bool is_down);
    void on_ok_count(const std::any& payload);
    void on_train_progress(const std::any& payload);
    void on_train_complete(const std::any& payload);
    void on_train_fail(const std::any& payload);

    EventBus& event_bus_;
};

} // namespace factory
