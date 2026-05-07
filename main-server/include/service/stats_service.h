#pragma once
// ============================================================================
// stats_service.h — /stats/* 비즈니스 로직
// ============================================================================
// StatsDao + GoalDao 조합. goal_progress 계산을 위해 사용자의 daily_goal_min 필요.
// ============================================================================

#include "storage/dao.h"

namespace factory {

class StatsService {
public:
    explicit StatsService(ConnectionPool& pool)
        : stats_dao_(pool), goal_dao_(pool) {}

    StatsDao::TodayStats today(long long user_id) {
        auto goal = goal_dao_.find_by_user(user_id);
        int gm = goal.found ? goal.daily_goal_min : 0;
        return stats_dao_.get_today(user_id, gm);
    }

    std::vector<StatsDao::HourBucket>
    hourly(long long user_id, const std::string& date) {
        return stats_dao_.get_hourly(user_id, date);
    }

    StatsDao::PatternStats pattern(long long user_id) {
        return stats_dao_.get_pattern(user_id);
    }

    std::vector<StatsDao::DayBucket> weekly(long long user_id) {
        return stats_dao_.get_weekly(user_id);
    }

private:
    StatsDao stats_dao_;
    GoalDao  goal_dao_;
};

} // namespace factory
