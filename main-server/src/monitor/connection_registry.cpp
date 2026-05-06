// ============================================================================
// connection_registry.cpp — 하위 서버(AI/Training) 연결 fd + 타입 레지스트리
// ============================================================================
// v0.11.0 이후 server_type 자동 태깅을 지원해 HealthChecker 가 IP 하드코딩
// 없이 "역할(ai_inference_1, ai_training 등)" 만으로 생존 감지 가능하다.
//
// 용도 요약:
//   AckSender      : find_fd(addr) 로 ACK 대상 소켓 조회
//   AckSender(RELOAD): get_all_connections() 로 전체 추론서버 브로드캐스트
//   Router         : set_server_type(addr, type) 로 연결에 역할 태깅
//   HealthChecker  : find_addr_by_type(type) 로 연결된 실제 IP 획득
// ============================================================================
#include "monitor/connection_registry.h"

namespace factory {

// Meyers' Singleton — C++11 이후 스레드 안전 초기화 보장
ConnectionRegistry& ConnectionRegistry::instance() {
    static ConnectionRegistry registry;
    return registry;
}

// 새 연결 등록. 이미 있으면 덮어씀 (TCP 재연결 케이스 대응).
// 처음엔 server_type 은 비어있고, Router 가 나중에 set_server_type 으로 태깅.
void ConnectionRegistry::register_connection(const std::string& sender_addr, int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    ConnectionInfo info;
    info.fd          = client_fd;
    info.server_type = "";   // 패킷 도착 전까지는 미식별
    conn_map_[sender_addr] = info;
}

// 연결 해제. handle_client 루프 종료 시점에 호출. 없는 키도 안전.
void ConnectionRegistry::unregister_connection(const std::string& sender_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    conn_map_.erase(sender_addr);
}

// sender_addr → fd 조회. 없으면 -1.
int ConnectionRegistry::find_fd(const std::string& sender_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = conn_map_.find(sender_addr);
    return (it != conn_map_.end()) ? it->second.fd : -1;
}

// Router 가 패킷 내용에서 추론한 server_type 을 기록.
// 동일 값이면 실질 변경 없음 (성능 고려 불필요 — 락은 짧음).
void ConnectionRegistry::set_server_type(const std::string& sender_addr,
                                          const std::string& server_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = conn_map_.find(sender_addr);
    if (it != conn_map_.end()) {
        it->second.server_type = server_type;
    }
    // 등록 전이면 noop — 일반 흐름상 register_connection 이 먼저 호출됨
}

// server_type 이 일치하는 첫 연결의 addr 반환. 없으면 "".
// HealthChecker 가 "ai_inference_1 이 실제 어느 IP 에서 연결 중인지" 를 알고 싶을 때.
std::string ConnectionRegistry::find_addr_by_type(const std::string& server_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [addr, info] : conn_map_) {
        if (info.server_type == server_type) return addr;
    }
    return "";
}

// 기존 API 호환: addr → fd 맵 스냅샷 (server_type 생략).
// MODEL_RELOAD 브로드캐스트처럼 "fd 만 필요한" 호출자를 위해 유지.
std::unordered_map<std::string, int> ConnectionRegistry::get_all_connections() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, int> out;
    out.reserve(conn_map_.size());
    for (const auto& [addr, info] : conn_map_) {
        out[addr] = info.fd;
    }
    return out;
}

// 상세 스냅샷 — HealthChecker 등 server_type 이 필요한 호출자용.
std::unordered_map<std::string, ConnectionInfo>
ConnectionRegistry::get_all_connections_detailed() {
    std::lock_guard<std::mutex> lock(mutex_);
    return conn_map_;   // 구조체 복사 (소규모라 비용 미미)
}

} // namespace factory
