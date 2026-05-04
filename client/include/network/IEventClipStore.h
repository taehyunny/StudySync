#pragma once

#include "model/PostureEvent.h"

#include <string>

struct ClipRef {
    std::string uri;
    std::size_t frame_count = 0;
};

class IEventClipStore {
public:
    virtual ~IEventClipStore() = default;
    virtual ClipRef store_clip(const PostureEvent& event) = 0;
};

