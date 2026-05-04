#pragma once

#include "core/RingBuffer.h"
#include "model/Frame.h"

#include <cstddef>
#include <vector>

class EventShadowBuffer {
public:
    void push(const Frame& frame);
    std::vector<Frame> snapshot(std::size_t window_size = 15) const;

private:
    RingBuffer<Frame, 60> buf_;
};

