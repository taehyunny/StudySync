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
    std::string event_id;   // 서버 멱등 처리 키 (emit 시 자동 생성)
    std::string reason;
    std::vector<Frame> frames;
};

