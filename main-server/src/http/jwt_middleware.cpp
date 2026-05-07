// ============================================================================
// jwt_middleware.cpp
// ============================================================================
#include "http/jwt_middleware.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <jwt-cpp/jwt.h>
#include <chrono>

namespace factory::http {

JwtMiddleware::JwtMiddleware(std::string secret, std::string issuer)
    : secret_(std::move(secret)), issuer_(std::move(issuer)) {}

JwtMiddleware::AuthResult
JwtMiddleware::authenticate(const httplib::Request& req) const {
    AuthResult r;
    auto header = req.get_header_value("Authorization");
    if (header.empty()) { r.error = "missing Authorization header"; return r; }

    constexpr const char* PREFIX = "Bearer ";
    if (header.rfind(PREFIX, 0) != 0) {
        r.error = "Authorization must be 'Bearer <token>'";
        return r;
    }
    std::string token = header.substr(std::string(PREFIX).size());
    if (token.empty()) { r.error = "empty token"; return r; }

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer(issuer_);
        verifier.verify(decoded);

        std::string sub = decoded.get_subject();
        r.user_id = std::strtoll(sub.c_str(), nullptr, 10);
        if (r.user_id <= 0) { r.error = "invalid sub claim"; return r; }

        if (decoded.has_payload_claim("email")) {
            r.email = decoded.get_payload_claim("email").as_string();
        }
        if (decoded.has_payload_claim("role")) {
            r.role = decoded.get_payload_claim("role").as_string();
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("token verify failed: ") + e.what();
    }
    return r;
}

void JwtMiddleware::send_unauthorized(httplib::Response& res,
                                       const std::string& reason) const {
    log_warn("HTTP", "401 unauthorized | %s", reason.c_str());
    send_401(res, "invalid credentials");
}

std::string JwtMiddleware::issue(long long user_id,
                                  const std::string& email,
                                  const std::string& name,
                                  const std::string& role,
                                  int expires_sec) const {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(expires_sec);

    return jwt::create()
        .set_issuer(issuer_)
        .set_type("JWT")
        .set_subject(std::to_string(user_id))
        .set_payload_claim("email", jwt::claim(email))
        .set_payload_claim("name",  jwt::claim(name))
        .set_payload_claim("role",  jwt::claim(role))
        .set_issued_at(now)
        .set_expires_at(exp)
        .sign(jwt::algorithm::hs256{secret_});
}

} // namespace factory::http
