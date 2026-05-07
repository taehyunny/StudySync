// ============================================================================
// session_service.cpp
// ============================================================================
#include "service/session_service.h"
#include "core/logger.h"

namespace factory {

// SessionService::start 위 — /session/start 시 같은 user 의 미종료 세션을
// 자동 마감해서 orphan 누적 방지.
long long SessionService::start_with_cleanup(long long user_id,
                                              const std::string& start_time) {
    if (user_id <= 0 || start_time.empty()) return -1;

    // 1) 기존 미종료 세션 강제 마감 (있으면)
    int closed = session_dao_.force_close_user_open_sessions(user_id, start_time);
    if (closed > 0) {
        log_main("session/start: 이전 미종료 세션 %d개 자동 마감 | user=%lld",
                 closed, user_id);
    }

    // 2) 새 세션 생성
    return session_dao_.start(user_id, start_time);
}

SessionService::EndResult
SessionService::end(long long user_id, long long session_id,
                    const std::string& end_time) {
    EndResult r;
    if (user_id <= 0 || session_id <= 0) return r;

    // 소유권 검증
    auto info = session_dao_.find_by_id(session_id);
    if (!info.found || info.user_id != user_id) {
        log_warn("HTTP", "session/end 소유권 불일치 | user=%lld sid=%lld owner=%lld",
                 user_id, session_id, info.user_id);
        return r;
    }

    // focus_logs 집계
    auto agg = session_dao_.aggregate(session_id);
    r.focus_min = agg.focus_min;
    r.avg_focus = agg.avg_focus;

    // 오늘 누적 focus_min vs daily_goal_min 비교 → goal_achieved
    // TODO(spec): 정확한 goal_achieved 정의 미정.
    //   잠정: 이 세션의 focus_min 단독 >= daily_goal_min 으로 판정.
    //   합의되면 "오늘 누적" 으로 변경.
    auto goal = goal_dao_.find_by_user(user_id);
    int goal_min = goal.found ? goal.daily_goal_min : 0;
    r.goal_achieved = (goal_min > 0) && (agg.focus_min >= static_cast<double>(goal_min));

    if (!session_dao_.end(session_id, end_time, agg.focus_min, agg.avg_focus,
                          r.goal_achieved)) {
        return r;
    }

    r.ok = true;
    log_main("session 종료 | sid=%lld user=%lld focus_min=%.2f avg=%.3f goal=%d",
             session_id, user_id, agg.focus_min, agg.avg_focus,
             r.goal_achieved ? 1 : 0);
    return r;
}

} // namespace factory
