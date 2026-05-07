// ============================================================================
// auth_controller.cpp — /auth/register, /auth/login
// ============================================================================
// 클라 스펙 §3-1, §3-2 준수.
// ============================================================================
#include "http/controllers/auth_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace factory::http {

AuthController::AuthController(HttpServer& server, UserService& service)
    : server_(server), service_(service) {}

namespace {

const char* code_to_message(UserService::Code c) {
    using C = UserService::Code;
    switch (c) {
        case C::Ok:                  return "ok";
        case C::InvalidEmail:        return "invalid email format";
        case C::InvalidPassword:     return "invalid password";
        case C::InvalidName:         return "invalid name";
        case C::EmailExists:         return "email already exists";
        case C::InvalidCredentials:  return "invalid credentials";
        case C::Internal:            return "internal error";
    }
    return "unknown";
}

int code_to_http(UserService::Code c, bool is_register) {
    using C = UserService::Code;
    switch (c) {
        case C::Ok:                  return is_register ? 201 : 200;
        case C::InvalidEmail:
        case C::InvalidPassword:
        case C::InvalidName:         return 400;
        case C::EmailExists:         return 409;
        case C::InvalidCredentials:  return 401;
        case C::Internal:            return 500;
    }
    return 500;
}

} // anon

void AuthController::register_routes() {
    auto& svr = server_.raw();

    // ── POST /auth/register ───────────────────────────────────────
    svr.Post("/auth/register",
             [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string email = body.value("email", "");
            std::string password = body.value("password", "");
            std::string name = body.value("name", "");

            auto r = service_.register_user(email, password, name);
            int status = code_to_http(r.code, /*is_register=*/true);

            json out = { {"code", status}, {"message", code_to_message(r.code)} };
            if (r.code == UserService::Code::Ok) out["user_id"] = r.user_id;
            send_json(res, status, out);
        } catch (const std::exception& e) {
            log_err_clt("register parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });

    // ── POST /auth/login ──────────────────────────────────────────
    svr.Post("/auth/login",
             [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string email = body.value("email", "");
            std::string password = body.value("password", "");

            auto r = service_.login(email, password);
            int status = code_to_http(r.code, /*is_register=*/false);

            if (r.code == UserService::Code::Ok) {
                send_json(res, status, {
                    {"code",         status},
                    // 클라 AuthApi.cpp 가 "access_token" 으로 추출. 호환을 위해 둘 다 박음.
                    {"access_token", r.token},
                    {"token",        r.token},
                    {"user_id",      r.user_id},
                    {"name",         r.name}
                });
            } else {
                const char* msg = code_to_message(r.code);
                send_json(res, status, {
                    {"code",    status},
                    {"message", msg},
                    // 클라 AuthApi.cpp 가 message 비어있으면 "detail" 로 fallback. 동일 메시지 alias.
                    {"detail",  msg}
                });
            }
        } catch (const std::exception& e) {
            log_err_clt("login parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });
}

} // namespace factory::http
