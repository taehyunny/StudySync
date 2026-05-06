// ============================================================================
// gui_tcp_listener.h — MFC GUI 클라이언트 전용 TCP 리스너
// ============================================================================
// 책임: TCP 접속 수락 + 패킷 수신만 담당.
// 수신된 JSON은 GuiRouter에 전달하여 프로토콜별 처리를 위임한다.
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "session/gui_router.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace factory {

class GuiTcpListener {
public:
    GuiTcpListener(EventBus& bus, uint16_t port, GuiRouter& router);
    ~GuiTcpListener();

    void start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd, const std::string& remote_addr);

    /// 한 프레임 수신. JSON 에 "image_size" > 0 필드가 있으면 그 바이트만큼 추가로
    /// 읽어 out_binary 에 담는다. 없으면 out_binary 는 비어있음.
    /// v0.13.0: RETRAIN_UPLOAD(158) 등 바이너리 동반 프로토콜 지원용.
    bool recv_one_request(int client_fd,
                          std::string& out_json,
                          std::vector<uint8_t>& out_binary);

    EventBus&         event_bus_;
    uint16_t          listen_port_;
    int               server_fd_;
    std::thread       accept_thread_;
    std::atomic<bool> is_running_;
    GuiRouter&        router_;
};

} // namespace factory
