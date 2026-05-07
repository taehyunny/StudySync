#pragma once
#include "http/http_server.h"
#include "http/jwt_middleware.h"
#include "service/log_service.h"

namespace factory::http {

class LogController {
public:
    LogController(HttpServer& server, JwtMiddleware& jwt, LogService& service);
    void register_routes();

private:
    HttpServer&    server_;
    JwtMiddleware& jwt_;
    LogService&    service_;
};

} // namespace factory::http
