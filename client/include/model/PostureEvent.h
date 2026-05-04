#pragma once

#include "model/Frame.h"

#include <cstdint>
#include <string>
#include <vector>

enum class PostureEventType {
    BadPosture,
    Drowsy,
    Absent
};

struct PostureEvent {
    PostureEventType type = PostureEventType::BadPosture;
    std::uint64_t timestamp_ms = 0;
    std::string reason;
    std::vector<Frame> frames;
};

