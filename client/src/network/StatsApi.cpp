#include "pch.h"
#include "network/StatsApi.h"

#include <chrono>
#include <sstream>

namespace {
constexpr const char* kFocusKeys[] = { "focus_min", "today_focus_min", "focus_minutes", "study_min", nullptr };
constexpr const char* kBreakKeys[] = { "break_min", "rest_min", "break_minutes", "today_break_min", nullptr };
constexpr const char* kWarningKeys[] = { "warning_count", "event_count", "alert_count", "posture_warning_count", nullptr };
constexpr const char* kAvgFocusKeys[] = { "avg_focus", "average_focus", "focus_avg", "avg_focus_score", nullptr };
}

StatsApi::StatsApi(WinHttpClient& http)
    : http_(http)
{
}

ServerStatsSnapshot::Data StatsApi::today()
{
    ServerStatsSnapshot::Data data;

    const HttpResponse response = http_.get("/stats/today");
    if (!response.ok()) {
        std::ostringstream log;
        log << "[StudySync][StatsApi] /stats/today failed HTTP "
            << response.status_code << " body=" << response.body << "\n";
        OutputDebugStringA(log.str().c_str());
        return data;
    }

    data.focus_min = extract_int_any(response.body, kFocusKeys);
    data.break_min = extract_int_any(response.body, kBreakKeys);
    data.warning_count = extract_int_any(response.body, kWarningKeys);
    data.avg_focus = extract_double_any(response.body, kAvgFocusKeys);
    data.fetched_at_ms = now_ms();
    data.valid = true;

    std::ostringstream log;
    log << "[StudySync][StatsApi] today focus_min=" << data.focus_min
        << " break_min=" << data.break_min
        << " avg_focus=" << data.avg_focus
        << " warnings=" << data.warning_count << "\n";
    OutputDebugStringA(log.str().c_str());
    return data;
}

int StatsApi::extract_int_any(const std::string& json, const char* const* keys, int fallback)
{
    for (int i = 0; keys[i] != nullptr; ++i) {
        const double value = extract_number(json, keys[i], -1.0);
        if (value >= 0.0) return static_cast<int>(value);
    }
    return fallback;
}

double StatsApi::extract_double_any(const std::string& json, const char* const* keys, double fallback)
{
    for (int i = 0; keys[i] != nullptr; ++i) {
        const double value = extract_number(json, keys[i], -1.0);
        if (value >= 0.0) return value;
    }
    return fallback;
}

double StatsApi::extract_number(const std::string& json, const std::string& key, double fallback)
{
    const std::string pattern = "\"" + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return fallback;

    pos += pattern.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;

    std::string value;
    for (std::size_t i = pos; i < json.size(); ++i) {
        const char ch = json[i];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E') {
            value += ch;
        } else {
            break;
        }
    }

    if (value.empty()) return fallback;
    try {
        return std::stod(value);
    } catch (...) {
        OutputDebugStringA("[StudySync][StatsApi] numeric parse failed\n");
        return fallback;
    }
}

std::uint64_t StatsApi::now_ms()
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}
