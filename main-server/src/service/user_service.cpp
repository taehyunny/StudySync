// ============================================================================
// user_service.cpp
// ============================================================================
#include "service/user_service.h"
#include "storage/password_hash.h"
#include "core/logger.h"

#include <regex>

namespace factory {

namespace {

// 이메일 형식 — 가벼운 검증. 정확한 RFC 검증은 과함.
bool is_valid_email(const std::string& s) {
    if (s.empty() || s.size() > 255) return false;
    static const std::regex re(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    return std::regex_match(s, re);
}

bool is_valid_password(const std::string& s) {
    return s.size() >= 4 && s.size() <= 128;   // 임시 정책 — 클라 합의 후 강화
}

bool is_valid_name(const std::string& s) {
    return !s.empty() && s.size() <= 100;
}

} // anon

UserService::UserService(ConnectionPool& pool,
                         http::JwtMiddleware& jwt,
                         int jwt_expires_sec)
    : user_dao_(pool), jwt_(jwt), jwt_expires_sec_(jwt_expires_sec) {}

UserService::RegisterResult
UserService::register_user(const std::string& email,
                            const std::string& password,
                            const std::string& name) {
    RegisterResult r;
    if (!is_valid_email(email))     { r.code = Code::InvalidEmail;    return r; }
    if (!is_valid_password(password)){ r.code = Code::InvalidPassword; return r; }
    if (!is_valid_name(name))       { r.code = Code::InvalidName;     return r; }

    if (user_dao_.exists_by_email(email)) {
        r.code = Code::EmailExists;
        return r;
    }

    long long uid = user_dao_.insert(email, password, name, "user");
    if (uid <= 0) {
        // exists_by_email 후 race 또는 DB 에러
        if (user_dao_.exists_by_email(email)) r.code = Code::EmailExists;
        else                                  r.code = Code::Internal;
        return r;
    }

    r.code    = Code::Ok;
    r.user_id = uid;
    log_clt("회원가입 | id=%lld email=%s", uid, email.c_str());
    return r;
}

UserService::LoginResult
UserService::login(const std::string& email, const std::string& password) {
    LoginResult r;
    if (!is_valid_email(email) || password.empty()) {
        r.code = Code::InvalidCredentials;
        return r;
    }

    auto user = user_dao_.find_by_email(email);
    if (!user.found) {
        r.code = Code::InvalidCredentials;
        return r;
    }

    if (!PasswordHash::verify(password, user.password_hash)) {
        r.code = Code::InvalidCredentials;
        return r;
    }

    r.token = jwt_.issue(user.id, user.email, user.name,
                         user.role.empty() ? "user" : user.role,
                         jwt_expires_sec_);
    if (r.token.empty()) {
        r.code = Code::Internal;
        return r;
    }

    r.code    = Code::Ok;
    r.user_id = user.id;
    r.name    = user.name;
    log_clt("로그인 성공 | id=%lld email=%s", user.id, email.c_str());
    return r;
}

} // namespace factory
