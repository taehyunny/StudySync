// ============================================================================
// stats_controller.cpp — GET /stats/today, /hourly, /pattern, /weekly
// 클라 스펙 §3-7 ~ §3-10
// ============================================================================
#include "http/controllers/stats_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <regex>

using nlohmann::json;

namespace factory::http {

namespace {
bool is_iso_date(const std::string& s) {
    static const std::regex re(R"(^\d{4}-\d{2}-\d{2}$)");
    return std::regex_match(s, re);
}
} // anon

StatsController::StatsController(HttpServer& server, JwtMiddleware& jwt,
                                  StatsService& service)
    : server_(server), jwt_(jwt), service_(service) {}

void StatsController::register_routes() {
    auto& svr = server_.raw();

    // GET /stats/today
    svr.Get("/stats/today",
            [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        auto t = service_.today(auth.user_id);
        send_json(res, 200, {
            {"code",          200},
            {"focus_min",     t.focus_min},
            {"avg_focus",     t.avg_focus},
            {"warning_count", t.warning_count},
            {"goal_progress", t.goal_progress}
        });
    });

    // GET /stats/hourly?date=YYYY-MM-DD
    svr.Get("/stats/hourly",
            [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        std::string date = req.get_param_value("date");
        if (date.empty() || !is_iso_date(date)) {
            send_400(res, "date query param required (YYYY-MM-DD)");
            return;
        }
        auto rows = service_.hourly(auth.user_id, date);
        json data = json::array();
        for (auto& b : rows) {
            data.push_back({{"hour", b.hour}, {"avg_focus", b.avg_focus}});
        }
        send_json(res, 200, { {"code", 200}, {"data", data} });
    });

    // GET /stats/pattern
    svr.Get("/stats/pattern",
            [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        auto p = service_.pattern(auth.user_id);
        send_json(res, 200, {
            {"code",               200},
            {"avg_focus_duration", p.avg_focus_duration},
            {"best_hour",          p.best_hour},
            {"weekly_avg",         p.weekly_avg}
        });
    });

    // GET /stats/weekly
    svr.Get("/stats/weekly",
            [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        auto rows = service_.weekly(auth.user_id);
        json data = json::array();
        for (auto& d : rows) {
            data.push_back({
                {"date",      d.date},
                {"focus_min", d.focus_min},
                {"avg_focus", d.avg_focus}
            });
        }
        send_json(res, 200, { {"code", 200}, {"data", data} });
    });
}

} // namespace factory::http
