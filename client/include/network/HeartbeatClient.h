#pragma once

#include "network/ConnectionState.h"
#include "network/ReconnectPolicy.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class HeartbeatClient {
public:
    using Probe = std::function<bool()>;

    HeartbeatClient(
        std::string name,
        std::string endpoint,
        std::chrono::milliseconds healthy_interval = std::chrono::milliseconds(3000),
        std::uint32_t warning_threshold = 1,
        std::uint32_t disconnect_threshold = 3);
    ~HeartbeatClient();

    void start(Probe probe);
    void stop();
    ConnectionSnapshot snapshot() const;

private:
    void run();
    void mark_attempt();
    void mark_success();
    void mark_failure();
    void set_status(ConnectionStatus status);
    void log_state(const char* message) const;

    std::string name_;
    std::string endpoint_;
    std::chrono::milliseconds healthy_interval_;
    std::uint32_t warning_threshold_;
    std::uint32_t disconnect_threshold_;
    Probe probe_;
    ReconnectPolicy reconnect_policy_;

    mutable std::mutex mtx_;
    ConnectionSnapshot snapshot_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};

