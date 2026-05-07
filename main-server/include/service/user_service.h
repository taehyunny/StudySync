#pragma once
// ============================================================================
// user_service.h — 회원가입 / 로그인 비즈니스 로직
// ============================================================================
// AuthController 가 호출. UserDao + PasswordHash + JwtMiddleware 조합.
// 응답에 들어갈 정보(token, user_id, name)까지 만들어서 반환.
// ============================================================================

#include "storage/dao.h"
#include "http/jwt_middleware.h"

namespace factory {

class UserService {
public:
    UserService(ConnectionPool& pool, http::JwtMiddleware& jwt, int jwt_expires_sec);

    enum class Code {
        Ok = 0,
        InvalidEmail,
        InvalidPassword,
        InvalidName,
        EmailExists,
        InvalidCredentials,
        Internal,
    };

    struct RegisterResult {
        Code      code    = Code::Internal;
        long long user_id = 0;
    };
    RegisterResult register_user(const std::string& email,
                                  const std::string& password,
                                  const std::string& name);

    struct LoginResult {
        Code        code    = Code::Internal;
        long long   user_id = 0;
        std::string name;
        std::string token;
    };
    LoginResult login(const std::string& email, const std::string& password);

private:
    UserDao              user_dao_;
    http::JwtMiddleware& jwt_;
    int                  jwt_expires_sec_;
};

} // namespace factory
