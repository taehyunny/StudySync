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

    // 성공 시 신규 session_id, 실패 시 -1. (단순 — 미종료 세션 정리 안 함)
    long long start(long long user_id, const std::string& start_time) {
        return session_dao_.start(user_id, start_time);
    }

    // 새 세션 시작 전 같은 user 의 미종료 세션을 자동 마감.
    // 클라가 비정상 종료 후 재시작하는 시나리오 안전 처리.
    // 컨트롤러는 이걸 호출해 자동 정리 효과 얻음.
    long long start_with_cleanup(long long user_id, const std::string& start_time);

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
