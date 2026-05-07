#pragma once

#include "network/WinHttpClient.h"
#include <string>

// ============================================================================
// SessionApi — 학습 세션 시작/종료 HTTP API
// ============================================================================
// POST /session/start  → session_id 반환
// POST /session/end    → 집계 결과 반환
// ============================================================================

struct SessionStartResult {
    bool      success    = false;
    long long session_id = 0;
    std::string message;
};

struct SessionEndResult {
    bool  success       = false;
    int   focus_min     = 0;
    float avg_focus     = 0.0f;
    bool  goal_achieved = false;
    std::string message;
};

class SessionApi {
public:
    explicit SessionApi(WinHttpClient& http);

    // 세션 시작 — ISO8601 시작 시각을 서버에 전송하고 session_id 수신
    SessionStartResult start(const std::string& iso_start_time);

    // 세션 종료 — session_id + 종료 시각 전송
    SessionEndResult end(long long session_id, const std::string& iso_end_time);

private:
    static std::string now_iso8601();
    static long long   extract_int64(const std::string& json, const std::string& key);
    static int         extract_int  (const std::string& json, const std::string& key);
    static float       extract_float(const std::string& json, const std::string& key);
    static std::string extract_str  (const std::string& json, const std::string& key);

    WinHttpClient& http_;
};
