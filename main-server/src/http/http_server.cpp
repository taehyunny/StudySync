// ============================================================================
// http_server.cpp
// ============================================================================
#include "http/http_server.h"
#include "core/logger.h"

namespace factory::http {

HttpServer::HttpServer(std::string host, int port)
    : host_(std::move(host)), port_(port), running_(false) {
    // 모든 응답 기본 헤더 — UTF-8 보장 + CORS (개발 편의)
    server_.set_default_headers({
        {"Access-Control-Allow-Origin",  "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Authorization, Content-Type"},
    });

    // OPTIONS preflight — cpp-httplib 는 자동 처리 안 함
    server_.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // 헬스체크
    server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"code\":200,\"message\":\"ok\"}",
                        "application/json; charset=utf-8");
    });

    // 모든 들어오는 요청 access log — 디버깅용 (어느 IP, 어떤 path)
    server_.set_pre_routing_handler(
        [](const httplib::Request& req, httplib::Response&) {
            log_main("HTTP %s %s from %s (body=%zu)",
                     req.method.c_str(), req.path.c_str(),
                     req.remote_addr.c_str(), req.body.size());
            return httplib::Server::HandlerResponse::Unhandled;
        });
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    running_.store(true);
    listen_thread_ = std::thread([this]() {
        log_main("HTTP 서버 시작 | %s:%d", host_.c_str(), port_);
        if (!server_.listen(host_, port_)) {
            log_err_main("HTTP 서버 listen 실패 | %s:%d", host_.c_str(), port_);
        }
        log_main("HTTP 서버 종료 | %s:%d", host_.c_str(), port_);
    });
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    server_.stop();
    if (listen_thread_.joinable()) listen_thread_.join();
}

} // namespace factory::http
