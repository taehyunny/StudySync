// ============================================================================
// session_service.cpp
// ============================================================================
#include "service/session_service.h"
#include "core/logger.h"

namespace factory {

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
