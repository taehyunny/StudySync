#pragma once
// ============================================================================
// goal_service.h — /goal POST/GET 비즈니스 로직
// ============================================================================
#include "storage/dao.h"

namespace factory {

class GoalService {
public:
    explicit GoalService(ConnectionPool& pool) : dao_(pool) {}

    bool set_goal(long long user_id,
                  int daily_goal_min,
                  int rest_interval_min,
                  int rest_duration_min) {
        return dao_.upsert(user_id, daily_goal_min, rest_interval_min, rest_duration_min);
    }

    GoalDao::GoalInfo get_goal(long long user_id) {
        return dao_.find_by_user(user_id);
    }

private:
    GoalDao dao_;
};

} // namespace factory
