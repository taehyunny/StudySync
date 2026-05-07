#pragma once
// ============================================================================
// session_service.h — /session/start, /session/end 비즈니스 로직
// ============================================================================
// session/end 시 focus_logs 집계 + goal_achieved 계산을 책임진다.
// SessionDao::aggregate + GoalDao 조합.
// ============================================================================

#include "storage/dao.h"

namespace factory {

class SessionService {
public:
    explicit SessionService(ConnectionPool& pool)
        : session_dao_(pool), goal_dao_(pool) {}

    // 성공 시 신규 session_id, 실패 시 -1.
    long long start(long long user_id, const std::string& start_time) {
        return session_dao_.start(user_id, start_time);
    }

    struct EndResult {
        bool   ok            = false;
        double focus_min     = 0.0;
        double avg_focus     = 0.0;   // 0~1
        bool   goal_achieved = false;
    };
    /// 세션 종료. focus_logs 집계 → sessions UPDATE → 결과 반환.
    /// session 이 user_id 소유가 아닐 수 있으므로 호출 전에 컨트롤러가 검증할 것.
    EndResult end(long long user_id, long long session_id,
                  const std::string& end_time);

private:
    SessionDao session_dao_;
    GoalDao    goal_dao_;
};

} // namespace factory
