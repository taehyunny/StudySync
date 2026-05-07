#pragma once
// ============================================================================
// http_server.h — cpp-httplib 래핑 + 컨트롤러 등록
// ============================================================================
// 별도 스레드에서 listen. main.cpp 의 종료 흐름에서 stop() 호출.
//
// 컨트롤러는 register_routes() 에서 한꺼번에 등록 — 각 컨트롤러는
// http::HttpServer& 를 받아서 자신의 라우트를 박는 구조.
// ============================================================================

#include <httplib.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace factory::http {

class HttpServer {
public:
    HttpServer(std::string host, int port);
    ~HttpServer();

    /// cpp-httplib 의 Server 객체를 외부 컨트롤러에서 직접 라우팅할 수 있게 노출.
    httplib::Server& raw() { return server_; }

    void start();
    void stop();

private:
    std::string       host_;
    int               port_;
    httplib::Server   server_;
    std::thread       listen_thread_;
    std::atomic<bool> running_;
};

} // namespace factory::http
