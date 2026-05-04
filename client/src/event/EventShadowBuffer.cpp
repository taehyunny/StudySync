#include "pch.h"
#include "event/EventShadowBuffer.h"

void EventShadowBuffer::push(const Frame& frame)
{
    buf_.push(frame);
}

std::vector<Frame> EventShadowBuffer::snapshot(std::size_t window_size) const
{
    return buf_.snapshot(window_size);
}

