// ============================================================================
// health_checker.cpp — ConnectionRegistry 기반 헬스체크 (v0.11.0 동적 감지)
// ============================================================================
// 동작 원리:
//   config.json 의 health_check.targets 에 정의된 각 서버(ai_inference_1/2,
//   ai_training) 가 "현재 연결되어 있는지" 를 ConnectionRegistry 에 등록된
//   server_type 으로 판단한다.
//
//   과거 방식은 target.ip 와 연결된 소켓의 출발지 IP 를 문자열 비교했으나,
//   실제 배포 PC 가 변경되면 config 를 매번 수정해야 하는 불편이 있었다.
//   v0.11.0 부터는 Router 가 수신 패킷에서 추론한 server_type 을
//   ConnectionRegistry 에 태깅하고, HealthChecker 는 이 태그로 매칭한다.
//
// 매칭 정책 (우선순위):
//   1) server_type == target.name (예: "ai_inference_1") 인 연결이 있으면 살아있음
//   2) 위가 실패하면 target.ip 가 비어있지 않은 경우에 한해 IP prefix 매칭
//      (구버전 호환 — 원한다면 config.json 에 IP 를 여전히 적을 수 있음)
//
// 로그 개선:
//   생존 시 "서버 생존 | ai_inference_1 @ 10.x.x.x:PORT" 처럼 **실제 접속 IP**
//   를 표시해서 어느 PC 에서 접속했는지 즉시 파악 가능.
// ============================================================================
#include "monitor/health_checker.h"
#include "monitor/connection_registry.h"
#include "core/logger.h"
#include "core/tcp_utils.h"
#include "Protocol.h"

#include <algorithm>
#include <sstream>

namespace factory {

HealthChecker::HealthChecker(EventBus& bus,
                             std::vector<HealthTarget> targets,
                             std::chrono::seconds interval)
    : event_bus_(bus),
      targets_(std::move(targets)),
      interval_(interval),
      is_running_(false) {
    // 초기 상태: 모든 서버를 장애(미연결) 로 간주 — 첫 tick 에서 실제 상태로 수렴
    for (const auto& t : targets_) {
        down_state_map_[t.name] = true;
    }
}

HealthChecker::~HealthChecker() {
    stop();
}

void HealthChecker::start() {
    if (is_running_.exchange(true)) return;
    worker_thread_ = std::thread(&HealthChecker::run_loop, this);
}

void HealthChecker::stop() {
    if (!is_running_.exchange(false)) return;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

// ---------------------------------------------------------------------------
// run_loop — 주기적으로 모든 target 의 생존 여부를 판정하고 전환 시 이벤트 발행
//
// 각 tick 에서:
//   1) ConnectionRegistry::get_all_connections_detailed() 로 현재 연결 스냅샷
//   2) 각 target 에 대해:
//      - server_type 매칭으로 연결된 addr 찾기
//      - 없으면 (backward-compat) target.ip prefix 매칭 fallback
//      - 생존 → 과거 down 이었다면 SERVER_RECOVERED 발행
//      - 미연결 → 과거 up 이었다면 SERVER_DOWN 발행
//
// 주기 동적 조정:
//   모든 target 이 정상 → 30 초, 하나라도 장애 → config 의 interval_ (5 초 기본)
// ---------------------------------------------------------------------------
void HealthChecker::run_loop() {
    while (is_running_.load()) {
        auto connections = ConnectionRegistry::instance().get_all_connections_detailed();
        int connected_count = static_cast<int>(connections.size());

        // v0.13.1: 미태깅 연결들에 능동 HEALTH_PING 송신 → 즉각 server_type 획득
        //   AI 서버가 막 접속했지만 아직 INSPECT_META/OK_COUNT 를 안 보낸 상태에서는
        //   ConnectionRegistry 에 server_type 이 비어있다. 5초 주기 HealthChecker 가
        //   이 연결을 "미연결" 로 잘못 보고하지 않도록 PING 을 먼저 날려
        //   HEALTH_PONG(1201) 으로 server_type 을 즉시 알아낸다.
        for (const auto& [addr, info] : connections) {
            if (info.server_type.empty() && info.fd >= 0) {
                std::ostringstream ping;
                ping << "{\"protocol_no\":" << static_cast<int>(ProtocolNo::HEALTH_PING)
                     << ",\"protocol_version\":\"1.0\""
                     << ",\"image_size\":0"
                     << "}";
                (void)send_json_frame(info.fd, ping.str());   // 실패해도 조용히
            }
        }

        for (const auto& target : targets_) {
            // ── 1순위: server_type 매칭 (동적) ──────────────────────────
            std::string matched_addr;
            for (const auto& [addr, info] : connections) {
                if (info.server_type == target.name) {
                    matched_addr = addr;
                    break;
                }
            }

            // ── 2순위: target.ip 가 명시되어 있으면 IP prefix fallback ──
            //   config.json 에서 "ip" 를 지워 두면 이 블록은 안 탐.
            //   구버전 호환 목적으로만 유지.
            if (matched_addr.empty() && !target.ip.empty()) {
                std::string ip_prefix = target.ip + ":";
                for (const auto& [addr, info] : connections) {
                    if (addr.rfind(ip_prefix, 0) == 0) {
                        matched_addr = addr;
                        break;
                    }
                }
            }

            bool alive = !matched_addr.empty();

            // v0.14.7: 매 tick 마다 현재 상태를 **강제 이벤트 발행** — GUI 클라가 나중에
            //   접속했거나 로그인 직후 초기 sync 를 놓쳤어도 다음 tick(5~30초)에는 반드시
            //   HEALTH_PUSH 를 받게 됨. 이전엔 상태 "전환" 때만 발행해서 계속 down 상태면
            //   재접속한 GUI 가 영원히 Unknown(회색) 으로 남던 문제 해결.
            ServerStatusEvent ev{target.name, alive ? matched_addr : target.ip, target.port};
            if (alive) {
                event_bus_.publish(EventType::SERVER_RECOVERED, ev);
                if (down_state_map_[target.name]) {
                    log_main("서버 복구 감지 | %s @ %s",
                             target.name.c_str(), matched_addr.c_str());
                    down_state_map_[target.name] = false;
                }
                log_main("서버 생존 확인 | %s @ %s",
                         target.name.c_str(), matched_addr.c_str());
            } else {
                event_bus_.publish(EventType::SERVER_DOWN, ev);
                if (!down_state_map_[target.name]) {
                    log_err_main("서버 장애 감지 | %s", target.name.c_str());
                    down_state_map_[target.name] = true;
                } else {
                    log_err_main("서버 미연결 | %s", target.name.c_str());
                }
            }
        }

        log_main("연결 현황 | 총 %d개 AI서버 접속 중", connected_count);

        // 체크 주기 동적 조정 — 정상일 땐 로그 노이즈 최소화
        bool all_alive = true;
        for (const auto& target : targets_) {
            if (down_state_map_[target.name]) { all_alive = false; break; }
        }
        auto remaining = all_alive ? std::chrono::seconds(30) : interval_;

        while (is_running_.load() && remaining.count() > 0) {
            auto step = std::min(remaining, std::chrono::seconds(1));
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
    }
}

} // namespace factory
