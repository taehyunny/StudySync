#pragma once
// ============================================================================
// session_cleanup_worker.h — 주기적 stale session 정리
// ============================================================================
// 별도 스레드에서 N분마다 깨어나 SessionDao::force_close_stale_sessions() 호출.
// 클라가 비정상 종료된 후 영원히 매달려 있는 sessions row 정리.
//
// 시작/종료 시점:
//   main.cpp 에서 start() / stop() 호출.
//   stop() 시 worker 스레드가 즉시 깨어나 종료.
// ============================================================================

#include "storage/dao.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace factory {

class SessionCleanupWorker {
public:
    /// @param pool         DB 커넥션 풀 (SessionDao 가 사용)
    /// @param interval_min 깨어날 주기 (분, 기본 30분)
    /// @param stale_hours  몇 시간 이상 미종료면 stale 로 판단 (기본 6h)
    SessionCleanupWorker(ConnectionPool& pool,
                         int interval_min = 30,
                         int stale_hours  = 6);
    ~SessionCleanupWorker();

    void start();
    void stop();

private:
    void run();

    SessionDao             session_dao_;
    int                    interval_min_;
    int                    stale_hours_;
    std::atomic<bool>      running_{false};
    std::thread            worker_;
    std::mutex             mtx_;
    std::condition_variable cv_;
};

} // namespace factory
