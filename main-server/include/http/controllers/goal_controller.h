#pragma once
#include "http/http_server.h"
#include "http/jwt_middleware.h"
#include "service/goal_service.h"

namespace factory::http {

class GoalController {
public:
    GoalController(HttpServer& server, JwtMiddleware& jwt, GoalService& service);
    void register_routes();

private:
    HttpServer&    server_;
    JwtMiddleware& jwt_;
    GoalService&   service_;
};

} // namespace factory::http
