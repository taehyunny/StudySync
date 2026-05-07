#pragma once

#include "model/PostureEvent.h"

#include <cstdint>
#include <string>

struct ClipRef {
    std::string clip_id;
    std::string uri;
    std::string access_kind = "local_only";
    std::string format = "jpeg_sequence";
    std::size_t frame_count = 0;
    std::uint32_t retention_days = 3;
    std::uint64_t created_at_ms = 0;
    std::uint64_t expires_at_ms = 0;
};

class IEventClipStore {
public:
    virtual ~IEventClipStore() = default;
    virtual ClipRef store_clip(const PostureEvent& event) = 0;
};
