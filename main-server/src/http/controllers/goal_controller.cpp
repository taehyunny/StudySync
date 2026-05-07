// ============================================================================
// goal_controller.cpp — POST/GET /goal (클라 스펙 §3-3, §3-4)
// ============================================================================
#include "http/controllers/goal_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace factory::http {

GoalController::GoalController(HttpServer& server,
                                JwtMiddleware& jwt,
                                GoalService& service)
    : server_(server), jwt_(jwt), service_(service) {}

void GoalController::register_routes() {
    auto& svr = server_.raw();

    // POST /goal
    svr.Post("/goal", [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        try {
            auto body = json::parse(req.body);
            int daily   = body.value("daily_goal_min",    0);
            int rest_iv = body.value("rest_interval_min", 0);
            int rest_dr = body.value("rest_duration_min", 0);

            if (daily <= 0 || rest_iv <= 0 || rest_dr <= 0) {
                send_400(res, "fields must be positive integers");
                return;
            }
            if (!service_.set_goal(auth.user_id, daily, rest_iv, rest_dr)) {
                send_500(res, "db error");
                return;
            }
            send_json(res, 200, { {"code", 200}, {"message", "ok"} });
        } catch (const std::exception& e) {
            log_err_clt("goal POST parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });

    // GET /goal
    svr.Get("/goal", [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        auto info = service_.get_goal(auth.user_id);
        if (!info.found) {
            send_404(res, "goal not set");
            return;
        }
        send_json(res, 200, {
            {"code",              200},
            {"daily_goal_min",    info.daily_goal_min},
            {"rest_interval_min", info.rest_interval_min},
            {"rest_duration_min", info.rest_duration_min},
            // DB 의 MySQL DATETIME ("2026-05-07 09:17:06") 그대로 반환.
            // 클라가 ISO8601 원하면 'T' + 타임존 후처리 가능.
            {"updated_at",        info.updated_at}
        });
    });
}

} // namespace factory::http
