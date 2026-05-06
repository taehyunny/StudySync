// ============================================================================
// connection_registry.h — 하위 서버(AI/Training) 연결 레지스트리
// ============================================================================
// 목적:
//   TcpListener 가 수락한 하위 서버(추론/학습) 연결의 fd 및 server_type 을
//   sender_addr("IP:PORT") 를 키로 보관한다.
//
// server_type 자동 태깅 (v0.11.0):
//   최초엔 빈 문자열("") 이지만, Router 가 수신 패킷의 내용으로 추론하여
//   set_server_type() 으로 태깅한다.
//     station_id=1 패킷   → "ai_inference_1"
//     station_id=2 패킷   → "ai_inference_2"
//     TRAIN_* 패킷 / HEALTH_PONG(server_type=training) → "ai_training"
//
// 용도:
//   - AckSender: sender_addr 로 fd 조회 (find_fd)
//   - HealthChecker: server_type 기반 생존 감지 (IP 하드코딩 제거)
//   - AckSender (MODEL_RELOAD): 전체 연결 브로드캐스트
//
// 참고:
//   GUI 클라이언트 세션은 SessionManager 가 별도로 관리한다.
//   이 레지스트리는 서버 간(MainServer ↔ 추론/학습 서버) 연결 전용이다.
// ============================================================================
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace factory {

/// 단일 하위 서버 연결의 상태 정보
struct ConnectionInfo {
    int         fd          = -1;    ///< TCP 파일 디스크립터
    std::string server_type;         ///< 예: "ai_inference_1", "ai_training" (미식별시 "")
};

class ConnectionRegistry {
public:
    static ConnectionRegistry& instance();

    void register_connection(const std::string& sender_addr, int client_fd);
    void unregister_connection(const std::string& sender_addr);

    /// 없으면 -1 반환
    int  find_fd(const std::string& sender_addr);

    /// Router 가 패킷을 분석해 server_type 을 추정하면 여기로 태깅.
    /// 이미 같은 값이 세팅되어 있으면 noop (빈번 호출에 대응).
    void set_server_type(const std::string& sender_addr,
                         const std::string& server_type);

    /// server_type → 가장 먼저 발견된 연결의 sender_addr 반환 (없으면 "")
    /// HealthChecker 가 "ai_inference_1 이 어느 IP 에서 연결되어 있는가" 를 조회할 때 사용.
    std::string find_addr_by_type(const std::string& server_type);

    /// 연결된 모든 서버 목록 반환 (MODEL_RELOAD 브로드캐스트용).
    /// 반환값은 addr → fd 맵 스냅샷 (기존 API 호환 유지).
    std::unordered_map<std::string, int> get_all_connections();

    /// 연결된 모든 서버를 상세 정보(fd + server_type) 와 함께 반환.
    /// HealthChecker 가 server_type 매칭용으로 사용.
    std::unordered_map<std::string, ConnectionInfo> get_all_connections_detailed();

private:
    ConnectionRegistry() = default;
    std::mutex                                        mutex_;
    std::unordered_map<std::string, ConnectionInfo>   conn_map_;
};

} // namespace factory
