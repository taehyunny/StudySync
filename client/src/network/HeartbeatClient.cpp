#include "pch.h"
#include "network/HeartbeatClient.h"

#include <sstream>
#include <utility>
#include <windows.h>

HeartbeatClient::HeartbeatClient(
    std::string name,
    std::string endpoint,
    std::chrono::milliseconds healthy_interval,
    std::uint32_t warning_threshold,
    std::uint32_t disconnect_threshold)
    : name_(std::move(name))
    , endpoint_(std::move(endpoint))
    , healthy_interval_(healthy_interval)
    , warning_threshold_(warning_threshold)
    , disconnect_threshold_(disconnect_threshold)
{
    snapshot_.name = name_;
    snapshot_.endpoint = endpoint_;
}

HeartbeatClient::~HeartbeatClient()
{
    stop();
}

void HeartbeatClient::start(Probe probe)
{
    if (running_.exchange(true)) {
        return;
    }

    probe_ = std::move(probe);
    set_status(ConnectionStatus::Connecting);
    worker_ = std::thread(&HeartbeatClient::run, this);
}

void HeartbeatClient::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    set_status(ConnectionStatus::Disconnected);
}

ConnectionSnapshot HeartbeatClient::snapshot() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return snapshot_;
}

void HeartbeatClient::run()
{
    while (running_) {
        mark_attempt();

        const bool ok = probe_ ? probe_() : false;
        if (ok) {
            mark_success();
            std::this_thread::sleep_for(healthy_interval_);
            continue;
        }

        mark_failure();
        const auto delay = reconnect_policy_.next_delay();
        std::this_thread::sleep_for(delay);
    }
}

void HeartbeatClient::mark_attempt()
{
    std::lock_guard<std::mutex> lock(mtx_);
    snapshot_.last_attempt = std::chrono::steady_clock::now();
}

void HeartbeatClient::mark_success()
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        snapshot_.status = ConnectionStatus::Connected;
        snapshot_.consecutive_failures = 0;
        snapshot_.last_success = std::chrono::steady_clock::now();
    }
    reconnect_policy_.reset();
    log_state("heartbeat ok");
}

void HeartbeatClient::mark_failure()
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        ++snapshot_.consecutive_failures;

        if (snapshot_.consecutive_failures >= disconnect_threshold_) {
            snapshot_.status = ConnectionStatus::Reconnecting;
        } else if (snapshot_.consecutive_failures >= warning_threshold_) {
            snapshot_.status = ConnectionStatus::Warning;
        }
    }
    log_state("heartbeat failed");
}

void HeartbeatClient::set_status(ConnectionStatus status)
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        snapshot_.status = status;
    }
    log_state("status changed");
}

void HeartbeatClient::log_state(const char* message) const
{
    const auto state = snapshot();
    std::ostringstream out;
    out << "[StudySync][Heartbeat] " << state.name
        << " endpoint=" << state.endpoint
        << " status=" << to_string(state.status)
        << " failures=" << state.consecutive_failures
        << " message=" << message << "\n";
    OutputDebugStringA(out.str().c_str());
}

