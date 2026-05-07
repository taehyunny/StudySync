#pragma once
#include "http/http_server.h"
#include "http/jwt_middleware.h"
#include "service/stats_service.h"

namespace factory::http {

class StatsController {
public:
    StatsController(HttpServer& server, JwtMiddleware& jwt, StatsService& service);
    void register_routes();

private:
    HttpServer&    server_;
    JwtMiddleware& jwt_;
    StatsService&  service_;
};

} // namespace factory::http
