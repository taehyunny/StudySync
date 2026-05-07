#include "pch.h"
#include "network/ReconnectPolicy.h"

#include <algorithm>

ReconnectPolicy::ReconnectPolicy(std::chrono::milliseconds min_delay, std::chrono::milliseconds max_delay)
    : min_delay_(min_delay)
    , max_delay_(max_delay)
{
}

void ReconnectPolicy::reset()
{
    failure_count_ = 0;
}

std::chrono::milliseconds ReconnectPolicy::next_delay()
{
    const auto shift = std::min<std::uint32_t>(failure_count_, 4);
    const auto scaled = min_delay_.count() * (1LL << shift);
    ++failure_count_;
    return std::chrono::milliseconds(std::min<long long>(scaled, max_delay_.count()));
}

std::uint32_t ReconnectPolicy::failure_count() const
{
    return failure_count_;
}

