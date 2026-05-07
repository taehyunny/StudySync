// ============================================================================
// log_controller.cpp — POST /focus, POST /posture
// ============================================================================
// 명세 (메인서버 요구사항 분석서 §10, §11):
//   /focus   : session_id, focus_score, state, is_absent, is_drowsy 저장
//   /posture : neck_angle, shoulder_diff, posture_ok, vs_baseline 저장
//
// 본문 형식 (단건/배치 모두 수용 — 클라 합의 전 임시):
//   단건  : { "session_id": ..., "ts": ..., "focus_score": ..., ... }
//   배치  : { "session_id": ..., "logs": [{ts, focus_score, ...}, ...] }
//
// TODO(spec): 단건/배치 단일화 결정 후 한쪽으로 정리.
// TODO(spec): focus_score 0~1 float vs 0~100 int — 현재 int 가정.
//   클라가 0.85 보낼 경우 round(value * 100) 으로 변환 필요.
// ============================================================================
#include "http/controllers/log_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace factory::http {

LogController::LogController(HttpServer& server, JwtMiddleware& jwt,
                              LogService& service)
    : server_(server), jwt_(jwt), service_(service) {}

namespace {

// JSON 객체 한 건 → FocusLogDao::Entry 매핑.
// session_id 는 외부에서 검증된 값을 인자로 주입 (요청 바디 또는 배치 헤더).
FocusLogDao::Entry parse_focus_entry(const json& obj, long long session_id) {
    FocusLogDao::Entry e{};
    e.session_id   = session_id;
    e.ts           = obj.value("ts", "");
    e.timestamp_ms = obj.value("timestamp_ms", 0LL);

    // focus_score: int 또는 float 둘 다 수용 (TODO: 합의 후 단일화)
    if (obj.contains("focus_score")) {
        const auto& v = obj.at("focus_score");
        if (v.is_number_integer()) e.focus_score = v.get<int>();
        else if (v.is_number_float()) {
            double f = v.get<double>();
            // 0~1 범위면 *100, 이미 0~100 이면 그대로
            e.focus_score = (f <= 1.0) ? static_cast<int>(f * 100 + 0.5)
                                       : static_cast<int>(f + 0.5);
        }
    }
    e.state     = obj.value("state",     std::string{});
    e.is_absent = obj.value("is_absent", false);
    e.is_drowsy = obj.value("is_drowsy", false);
    return e;
}

PostureLogDao::Entry parse_posture_entry(const json& obj, long long session_id) {
    PostureLogDao::Entry e{};
    e.session_id   = session_id;
    e.ts           = obj.value("ts", "");
    e.timestamp_ms = obj.value("timestamp_ms", 0LL);
    if (obj.contains("neck_angle") && obj["neck_angle"].is_number()) {
        e.has_neck_angle = true;
        e.neck_angle = obj["neck_angle"].get<double>();
    }
    if (obj.contains("shoulder_diff") && obj["shoulder_diff"].is_number()) {
        e.has_shoulder_diff = true;
        e.shoulder_diff = obj["shoulder_diff"].get<double>();
    }
    e.posture_ok = obj.value("posture_ok", true);
    if (obj.contains("vs_baseline") && obj["vs_baseline"].is_number()) {
        e.has_vs_baseline = true;
        e.vs_baseline = obj["vs_baseline"].get<double>();
    }
    return e;
}

} // anon

void LogController::register_routes() {
    auto& svr = server_.raw();

    // ── POST /focus ───────────────────────────────────────────────
    svr.Post("/focus", [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        try {
            auto body = json::parse(req.body);
            long long sid = body.value("session_id", 0LL);
            if (sid <= 0) { send_400(res, "session_id required"); return; }
            if (!service_.owns_session(auth.user_id, sid)) {
                send_404(res, "session not found");
                return;
            }

            int inserted = 0;
            if (body.contains("logs") && body["logs"].is_array()) {
                for (const auto& obj : body["logs"]) {
                    auto e = parse_focus_entry(obj, sid);
                    if (service_.insert_focus(e) > 0) ++inserted;
                }
            } else {
                auto e = parse_focus_entry(body, sid);
                if (service_.insert_focus(e) > 0) ++inserted;
            }
            send_json(res, 200, {
                {"code", 200}, {"message", "ok"}, {"inserted", inserted}
            });
        } catch (const std::exception& e) {
            log_err_clt("/focus parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });

    // ── POST /posture ─────────────────────────────────────────────
    svr.Post("/posture", [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        try {
            auto body = json::parse(req.body);
            long long sid = body.value("session_id", 0LL);
            if (sid <= 0) { send_400(res, "session_id required"); return; }
            if (!service_.owns_session(auth.user_id, sid)) {
                send_404(res, "session not found");
                return;
            }

            int inserted = 0;
            if (body.contains("logs") && body["logs"].is_array()) {
                for (const auto& obj : body["logs"]) {
                    auto e = parse_posture_entry(obj, sid);
                    if (service_.insert_posture(e) > 0) ++inserted;
                }
            } else {
                auto e = parse_posture_entry(body, sid);
                if (service_.insert_posture(e) > 0) ++inserted;
            }
            send_json(res, 200, {
                {"code", 200}, {"message", "ok"}, {"inserted", inserted}
            });
        } catch (const std::exception& e) {
            log_err_clt("/posture parse error | %s", e.what());
            send_400(res, "invalid json body");
        }
    });
}

} // namespace factory::http
