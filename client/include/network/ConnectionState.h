#pragma once

#include <chrono>
#include <cstdint>
#include <string>

enum class ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Warning,
    Reconnecting
};

struct ConnectionSnapshot {
    ConnectionStatus status = ConnectionStatus::Disconnected;
    std::string name;
    std::string endpoint;
    std::uint32_t consecutive_failures = 0;
    std::chrono::steady_clock::time_point last_success{};
    std::chrono::steady_clock::time_point last_attempt{};
};

const char* to_string(ConnectionStatus status);

