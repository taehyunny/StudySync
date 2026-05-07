#pragma once
#include "http/http_server.h"
#include "http/jwt_middleware.h"
#include "service/session_service.h"

namespace factory::http {

class SessionController {
public:
    SessionController(HttpServer& server, JwtMiddleware& jwt, SessionService& service);
    void register_routes();

private:
    HttpServer&     server_;
    JwtMiddleware&  jwt_;
    SessionService& service_;
};

} // namespace factory::http
