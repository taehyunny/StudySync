#pragma once

#include <chrono>
#include <cstdint>

class ReconnectPolicy {
public:
    ReconnectPolicy(
        std::chrono::milliseconds min_delay = std::chrono::milliseconds(1000),
        std::chrono::milliseconds max_delay = std::chrono::milliseconds(10000));

    void reset();
    std::chrono::milliseconds next_delay();
    std::uint32_t failure_count() const;

private:
    std::chrono::milliseconds min_delay_;
    std::chrono::milliseconds max_delay_;
    std::uint32_t failure_count_ = 0;
};

