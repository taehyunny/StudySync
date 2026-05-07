// ============================================================================
// session_controller.cpp — POST /session/start, POST /session/end
// 클라 스펙 §3-5, §3-6
// ============================================================================
#include "http/controllers/session_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace factory::http {

SessionController::SessionController(HttpServer& server,
                                      JwtMiddleware& jwt,
                                      SessionService& service)
    : server_(server), jwt_(jwt), service_(service) {}

void SessionController::register_routes() {
    auto& svr = server_.raw();

    // POST /session/start
    svr.Post("/session/start",
             [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        try {
            auto body = json::parse(req.body);
            std::string start_time = body.value("start_time", "");
            if (start_time.empty()) { send_400(res, "start_time required"); return; }

            long long sid = service_.start(auth.user_id, start_time);
            if (sid <= 0) { send_500(res, "session start failed"); return; }

            send_json(res, 200, { {"code", 200}, {"session_id", sid} });
        } catch (const std::exception& e) {
            log_err_clt("session/start parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });

    // POST /session/end
    svr.Post("/session/end",
             [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        try {
            auto body = json::parse(req.body);
            long long sid = body.value("session_id", 0LL);
            std::string end_time = body.value("end_time", "");
            if (sid <= 0 || end_time.empty()) {
                send_400(res, "session_id and end_time required");
                return;
            }

            auto r = service_.end(auth.user_id, sid, end_time);
            if (!r.ok) {
                // 소유권 불일치 시 404 가 자연스러우나 정확한 사유 노출은 보안상 X.
                send_404(res, "session not found");
                return;
            }
            send_json(res, 200, {
                {"code",          200},
                {"focus_min",     r.focus_min},
                {"avg_focus",     r.avg_focus},
                {"goal_achieved", r.goal_achieved}
            });
        } catch (const std::exception& e) {
            log_err_clt("session/end parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });
}

} // namespace factory::http
