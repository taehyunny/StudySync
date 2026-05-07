// ============================================================================
// log_ingest_controller.cpp — POST /log/ingest (NDJSON)
// ============================================================================
// 클라 [client/src/network/JsonlBatchUploader.cpp] 가 30 line 단위 NDJSON 으로 POST.
// 각 line 의 "kind" 필드로 dispatch.
//
// 미스매치 흡수 (클라 현재 line 포맷 vs 서버 DB):
//   - "drowsy"/"absent" (클라 키) → is_drowsy/is_absent (DB)
//   - "ear" (클라) → DB 컬럼 없음, 무시
//   - session_id: line 마다 필수 (클라 측 합의 후 박아주기로)
//   - event line 의 reason 만 있음 → event_type/severity 누락 시 서버가 잠정값 채움
// ============================================================================
#include "http/controllers/log_ingest_controller.h"
#include "http/error_response.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>

using nlohmann::json;

namespace factory::http {

namespace {

// epoch ms → "YYYY-MM-DD HH:MM:SS" (MySQL DATETIME)
std::string ms_to_mysql_datetime(long long ms) {
    if (ms <= 0) return "";
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

bool parse_analysis_line(const json& obj, long long session_id,
                         FocusLogDao::Entry& f, PostureLogDao::Entry& p) {
    long long ts_ms = obj.value("timestamp_ms", 0LL);
    std::string ts = obj.contains("timestamp") && obj["timestamp"].is_string()
                       ? obj["timestamp"].get<std::string>()
                       : ms_to_mysql_datetime(ts_ms);
    if (ts.empty()) return false;

    // 클라 키와 DB 키 매핑: drowsy→is_drowsy, absent→is_absent
    f.session_id   = session_id;
    f.ts           = ts;
    f.timestamp_ms = ts_ms;
    f.focus_score  = obj.value("focus_score", 0);
    f.state        = obj.value("state", std::string{});
    f.is_absent    = obj.value("absent",  obj.value("is_absent",  false));
    f.is_drowsy    = obj.value("drowsy",  obj.value("is_drowsy",  false));

    p.session_id   = session_id;
    p.ts           = ts;
    p.timestamp_ms = ts_ms;
    if (obj.contains("neck_angle") && obj["neck_angle"].is_number()) {
        p.has_neck_angle = true; p.neck_angle = obj["neck_angle"].get<double>();
    }
    if (obj.contains("shoulder_diff") && obj["shoulder_diff"].is_number()) {
        p.has_shoulder_diff = true; p.shoulder_diff = obj["shoulder_diff"].get<double>();
    }
    p.posture_ok = obj.value("posture_ok", true);
    if (obj.contains("vs_baseline") && obj["vs_baseline"].is_number()) {
        p.has_vs_baseline = true; p.vs_baseline = obj["vs_baseline"].get<double>();
    }
    return true;
}

bool parse_event_line(const json& obj, long long session_id,
                      PostureEventDao::Entry& e) {
    long long ts_ms = obj.value("timestamp_ms", 0LL);
    std::string ts = obj.contains("timestamp") && obj["timestamp"].is_string()
                       ? obj["timestamp"].get<std::string>()
                       : ms_to_mysql_datetime(ts_ms);
    if (ts.empty()) return false;

    e.session_id    = session_id;
    e.ts            = ts;
    e.timestamp_ms  = ts_ms;
    e.event_id      = obj.value("event_id",   std::string{});
    e.event_type    = obj.value("event_type", std::string{});
    // 클라 line 이 event_type 미지정이면 reason 기반 잠정 매핑 시도
    if (e.event_type.empty()) {
        std::string reason = obj.value("reason", std::string{});
        if      (reason.find("posture")  != std::string::npos) e.event_type = "bad_posture";
        else if (reason.find("drowsy")   != std::string::npos ||
                 reason.find("졸음")     != std::string::npos)  e.event_type = "drowsy";
        else if (reason.find("absent")   != std::string::npos ||
                 reason.find("이탈")     != std::string::npos)  e.event_type = "absent";
        else if (reason.find("rest")     != std::string::npos ||
                 reason.find("휴식")     != std::string::npos)  e.event_type = "rest_required";
        else                                                    e.event_type = "bad_posture";
    }
    if (e.event_id.empty()) {
        // 클라가 event_id 없으면 멱등성 보장 X. session_id+ts_ms 로 가짜 키 생성.
        std::ostringstream os;
        os << "evt-" << session_id << "-" << ts_ms;
        e.event_id = os.str();
    }
    e.severity      = obj.value("severity",   std::string{"warning"});
    e.reason        = obj.value("reason",     std::string{});
    e.clip_id       = obj.value("clip_id",    std::string{});
    e.clip_access   = obj.value("clip_access", std::string{"local_only"});
    e.clip_ref      = obj.value("clip_ref",   std::string{});
    e.clip_format   = obj.value("clip_format",std::string{});
    e.frame_count   = obj.value("frame_count",  0);
    e.retention_days= obj.value("retention_days", 3);
    if (obj.contains("expires_at_ms") && obj["expires_at_ms"].is_number_integer()) {
        e.has_expires_at_ms = true;
        e.expires_at_ms = obj["expires_at_ms"].get<long long>();
    }
    return true;
}

} // anon

LogIngestController::LogIngestController(HttpServer& server, JwtMiddleware& jwt,
                                          LogService& service)
    : server_(server), jwt_(jwt), service_(service) {}

void LogIngestController::register_routes() {
    auto& svr = server_.raw();

    svr.Post("/log/ingest",
             [this](const httplib::Request& req, httplib::Response& res) {
        auto auth = jwt_.authenticate(req);
        if (!auth.ok) { jwt_.send_unauthorized(res, auth.error); return; }

        if (req.body.empty()) {
            send_400(res, "empty body");
            return;
        }

        // session ownership cache (배치 안에 동일 session 반복) — find_by_id N번 호출 방지
        std::unordered_map<long long, bool> ownership_cache;
        auto owned = [&](long long sid) -> bool {
            if (sid <= 0) return false;
            auto it = ownership_cache.find(sid);
            if (it != ownership_cache.end()) return it->second;
            bool ok = service_.owns_session(auth.user_id, sid);
            ownership_cache[sid] = ok;
            return ok;
        };

        int analysis_count = 0, event_count = 0, skipped = 0;

        // NDJSON 라인 분리 — \n 단위. Content-Type 무관.
        std::istringstream stream(req.body);
        std::string line;
        while (std::getline(stream, line)) {
            // 빈 줄 / CR 처리
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            json obj;
            try {
                obj = json::parse(line);
            } catch (const std::exception&) {
                ++skipped;
                continue;
            }

            std::string kind = obj.value("kind", std::string{});
            long long session_id = obj.value("session_id", 0LL);
            if (!owned(session_id)) {
                ++skipped;
                continue;
            }

            if (kind == "analysis") {
                FocusLogDao::Entry f{};
                PostureLogDao::Entry p{};
                if (!parse_analysis_line(obj, session_id, f, p)) {
                    ++skipped; continue;
                }
                long long fid = service_.insert_focus(f);
                long long pid = service_.insert_posture(p);
                if (fid > 0 || pid > 0) ++analysis_count;
                else                    ++skipped;
            } else if (kind == "event") {
                PostureEventDao::Entry e{};
                if (!parse_event_line(obj, session_id, e)) { ++skipped; continue; }
                if (service_.insert_event(e) > 0) ++event_count;
                else                              ++skipped;
            } else {
                ++skipped;
            }
        }

        log_main("/log/ingest user=%lld analysis=%d event=%d skipped=%d",
                 auth.user_id, analysis_count, event_count, skipped);
        send_json(res, 200, {
            {"code", 200},
            {"message", "ok"},
            {"accepted", {{"analysis", analysis_count}, {"event", event_count}}},
            {"skipped", skipped}
        });
    });
}

} // namespace factory::http
