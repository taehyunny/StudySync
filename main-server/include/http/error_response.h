#pragma once
// ============================================================================
// error_response.h — 표준 응답 헬퍼
// ============================================================================
// 클라 스펙 §1: 모든 응답에 { "code": <int>, "message": "..." } 박힘.
// JSON 본문 만들 때 매번 같은 코드를 쓰지 않도록 헬퍼 모음.
// ============================================================================

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

namespace factory::http {

inline void send_json(httplib::Response& res, int status,
                      const nlohmann::json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

inline void send_error(httplib::Response& res, int status,
                       const std::string& message) {
    // 클라 AuthApi.cpp 가 message 없을 때 detail 로 fallback 하므로 alias 동시 박음.
    send_json(res, status, {
        {"code",    status},
        {"message", message},
        {"detail",  message}
    });
}

// 자주 쓰는 단축형
inline void send_400(httplib::Response& res, const std::string& msg = "bad request") {
    send_error(res, 400, msg);
}
inline void send_401(httplib::Response& res, const std::string& msg = "unauthorized") {
    send_error(res, 401, msg);
}
inline void send_404(httplib::Response& res, const std::string& msg = "not found") {
    send_error(res, 404, msg);
}
inline void send_409(httplib::Response& res, const std::string& msg = "conflict") {
    send_error(res, 409, msg);
}
inline void send_500(httplib::Response& res, const std::string& msg = "internal error") {
    send_error(res, 500, msg);
}

} // namespace factory::http
