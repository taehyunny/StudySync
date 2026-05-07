// ============================================================================
// session_cleanup_worker.cpp
// ============================================================================
#include "service/session_cleanup_worker.h"
#include "core/logger.h"

namespace factory {

SessionCleanupWorker::SessionCleanupWorker(ConnectionPool& pool,
                                            int interval_min,
                                            int stale_hours)
    : session_dao_(pool),
      interval_min_(interval_min > 0 ? interval_min : 30),
      stale_hours_(stale_hours > 0 ? stale_hours : 6) {}

SessionCleanupWorker::~SessionCleanupWorker() {
    stop();
}

void SessionCleanupWorker::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this]() { this->run(); });
    log_main("SessionCleanupWorker 시작 | interval=%d분 stale=%dh",
             interval_min_, stale_hours_);
}

void SessionCleanupWorker::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();   // 즉시 깨움
    if (worker_.joinable()) worker_.join();
    log_main("SessionCleanupWorker 종료");
}

void SessionCleanupWorker::run() {
    using namespace std::chrono;
    while (running_.load()) {
        // interval_min 분 대기. stop() 호출되면 cv_ notify 로 즉시 탈출.
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, minutes(interval_min_),
                     [this]() { return !running_.load(); });
        if (!running_.load()) break;
        lk.unlock();

        // 실제 정리
        try {
            int closed = session_dao_.force_close_stale_sessions(stale_hours_);
            if (closed > 0) {
                log_main("SessionCleanupWorker tick: %d 개 stale 세션 정리", closed);
            }
        } catch (const std::exception& e) {
            log_err_main("SessionCleanupWorker 예외 | %s", e.what());
        }
    }
}

} // namespace factory
