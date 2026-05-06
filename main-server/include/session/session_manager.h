// ============================================================================
// session_manager.h — GUI 클라이언트 세션 관리자 (v0.14.3 per-client sender)
// ============================================================================
// 목적:
//   MFC 클라이언트의 TCP 접속 세션을 관리한다.
//   접속한 클라이언트를 fd(파일 디스크립터) 기반으로 등록/해제하고,
//   특정 station 또는 전체 클라이언트에 JSON 메시지를 브로드캐스트한다.
//
// v0.14.3 변경 — 클라이언트별 독립 송신 워커:
//   이전엔 EventBus 워커 스레드가 for loop 로 모든 클라에 직렬 send.
//   느린 클라 한 명(3~6MB NG 이미지 수신 지연) 이 있으면 다른 클라 전송도
//   전부 블록 → 결국 keepalive 로 여러 세션 동시 끊김.
//   이제 **각 세션마다 송신 큐 + 전용 스레드** 를 둔다. broadcast 는 큐에 push
//   하고 즉시 반환 → 느린 클라 영향이 그 클라에만 한정.
//
// 드랍 정책:
//   큐가 MAX_QUEUE_SIZE(기본 16) 초과 시 가장 오래된 항목 제거(drop-oldest).
//   실시간 NG 푸시는 최신이 더 중요하므로 과거 항목 버리는 게 합리적.
//
// 스레드 안전:
//   모든 public 메서드는 내부 mutex 로 보호. 각 세션의 송신 스레드는 해당
//   세션의 큐 condition_variable 로만 동기화.
// ============================================================================
#pragma once

#include "core/event_bus.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factory {

// 송신 큐 항목 — JSON + 선택적 바이너리
struct OutgoingMessage {
    std::string          json;       // [4B len][JSON] 프레이밍으로 송신
    std::vector<uint8_t> binary;     // 비어있으면 JSON 만, 아니면 뒤에 바이너리
};

// 개별 GUI 클라이언트 세션 정보 + 전용 송신 스레드
struct GuiSession {
    int         client_fd    = -1;
    std::string remote_addr;          // "IP:PORT"
    std::string client_name;          // 클라이언트 식별 (예: "operator_1")
    int         subscribed_station = 0; // 0이면 전체 구독, 1/2면 해당 station만

    // v0.14.3: 전용 송신 큐 + 스레드 (unique_ptr 로 이동 가능하도록)
    //   sender_ 는 백그라운드에서 queue_ 를 비우며 소켓에 write
    //   queue_cv_ 가 깨우기 신호를 제공
    std::unique_ptr<std::deque<OutgoingMessage>> queue;
    std::unique_ptr<std::mutex>                  queue_mutex;
    std::unique_ptr<std::condition_variable>     queue_cv;
    std::unique_ptr<std::atomic<bool>>           running;   // 스레드 종료 플래그
    std::unique_ptr<std::thread>                 sender;    // 송신 워커
};

class SessionManager {
public:
    static SessionManager& instance();

    // 세션 등록/해제 (register 시 송신 스레드도 시작)
    void register_session(int client_fd, const std::string& remote_addr);
    void unregister_session(int client_fd);

    // 클라이언트 이름/구독 station 설정 (로그인 후)
    void set_client_info(int client_fd,
                         const std::string& client_name,
                         int subscribed_station);

    // 연결된 모든 클라이언트에 JSON broadcast (큐에 push 후 즉시 반환)
    // station_filter: 0이면 전체, 1/2이면 해당 station 구독자만
    void broadcast(const std::string& json_message, int station_filter = 0);

    // JSON + 바이너리(이미지) broadcast
    void broadcast_with_binary(const std::string& json_message,
                               const std::vector<uint8_t>& binary_data,
                               int station_filter = 0);

    // 특정 클라이언트에 JSON 전송 (큐에 push)
    bool send_to(int client_fd, const std::string& json_message);

    // 현재 연결된 세션 수
    std::size_t session_count() const;

    /// 같은 username으로 이미 로그인된 세션의 fd를 반환 (없으면 -1)
    int find_fd_by_username(const std::string& username) const;

    /// fd로부터 "IP:PORT" 문자열 조회 (없으면 빈 문자열)
    std::string get_remote_addr(int client_fd) const;

    /// 지정된 fd의 세션을 강제 종료 — 연결 끊고 세션 제거
    void force_close(int client_fd);

    /// v0.14.7: 만료된 "pending 해제" 항목을 로그로 flush.
    ///   같은 IP 가 3초 안에 재접속하면 해제 로그는 생략되고, 그 이상 지난 건
    ///   여기서 "진짜 해제" 로그로 발행됨. accept_loop 가 1초 주기로 호출.
    void flush_expired_disconnects();

private:
    SessionManager() = default;

    /// 큐에 OutgoingMessage 를 밀어넣음. 큐 상한 초과 시 오래된 항목 드랍.
    void enqueue_locked(GuiSession& sess, OutgoingMessage msg);

    /// 세션별 송신 스레드 본체 — 큐에서 메시지 꺼내 소켓에 write
    static void sender_loop(GuiSession* sess);

    /// 실제 소켓 송신 ([4B len][JSON] + 선택 바이너리)
    static bool send_frame(int fd, const OutgoingMessage& msg);

    // 세션별 송신 큐 상한
    static constexpr std::size_t MAX_QUEUE_SIZE = 16;

    mutable std::mutex                       mutex_;      // 세션 맵 동시 접근 보호
    std::unordered_map<int, GuiSession>      sessions_;   // key: client_fd
};

} // namespace factory
