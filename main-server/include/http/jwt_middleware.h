#pragma once
// ============================================================================
// jwt_middleware.h — Authorization: Bearer 검증
// ============================================================================
// 사용 패턴:
//   svr.Post("/goal", [&](const Request& req, Response& res) {
//       auto auth = jwt_mw.authenticate(req);
//       if (!auth.ok) { jwt_mw.send_unauthorized(res, auth.error); return; }
//       long long user_id = auth.user_id;
//       ... 핸들러 본문 ...
//   });
//
// 토큰 발급은 user_service 가 담당 (login 응답에서). 여기는 검증 + 클레임 추출만.
// ============================================================================

#include <httplib.h>
#include <string>

namespace factory::http {

class JwtMiddleware {
public:
    JwtMiddleware(std::string secret, std::string issuer = "studysync");

    struct AuthResult {
        bool        ok      = false;
        long long   user_id = 0;
        std::string email;
        std::string role;
        std::string error;   // ok=false 일 때 사유
    };

    /// Authorization 헤더 파싱 + 검증.
    /// 통과 시 user_id 등 클레임을 result 로 반환.
    AuthResult authenticate(const httplib::Request& req) const;

    /// 검증 실패 시 401 응답 한 줄로 반환할 헬퍼.
    void send_unauthorized(httplib::Response& res,
                           const std::string& reason) const;

    /// 토큰 발급 — login 핸들러 / user_service 에서 사용.
    /// expires_sec: 만료까지 초 수 (스펙 권장 86400 = 24시간).
    std::string issue(long long user_id,
                      const std::string& email,
                      const std::string& name,
                      const std::string& role,
                      int expires_sec) const;

private:
    std::string secret_;
    std::string issuer_;
};

} // namespace factory::http
