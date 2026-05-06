// ============================================================================
// health_checker.h — 하위 서버 헬스체크 (ConnectionRegistry 기반)
// ============================================================================
// 목적:
//   주기적으로(기본 5초) ConnectionRegistry에 등록된 AI서버 연결이
//   살아있는지 확인하고, 연결 수 변화를 감지하여 이벤트를 발행한다.
//
// 동작:
//   - ConnectionRegistry에 연결이 있으면 → 서버 생존
//   - 연결이 없으면 → 장애 감지 (SERVER_DOWN)
//   - 장애 상태에서 연결이 복귀하면 → SERVER_RECOVERED
//   - GuiNotifier가 이 이벤트를 받아 MFC 클라이언트에 알림 푸시
// ============================================================================
#pragma once

#include "core/event_bus.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factory {

// 감시 대상 서버 정의 — name으로 식별
struct HealthTarget {
    std::string name;        // 식별 이름 (예: "ai_inference_1")
    std::string ip;          // 서버 IP (로그 표시용)
    uint16_t    port = 0;    // 서버 포트 (로그 표시용)
};

class HealthChecker {
public:
    HealthChecker(EventBus& bus,
                  std::vector<HealthTarget> targets,
                  std::chrono::seconds interval = std::chrono::seconds(5));
    ~HealthChecker();

    void start();
    void stop();

private:
    void run_loop();

    EventBus&                       event_bus_;
    std::vector<HealthTarget>       targets_;
    std::chrono::seconds            interval_;
    // 서버별 현재 장애 상태 (true=장애, false=정상)
    std::unordered_map<std::string, bool> down_state_map_;

    std::thread                     worker_thread_;
    std::atomic<bool>               is_running_;
};

} // namespace factory
