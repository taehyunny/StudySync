#include "pch.h"
#include "event/EventShadowBuffer.h"

void EventShadowBuffer::push(const Frame& frame)
{
    buf_.push(frame);
}

std::vector<Frame> EventShadowBuffer::snapshot(std::size_t window_size) const
{
    std::vector<Frame> frames = buf_.snapshot(window_size);
    for (auto& frame : frames) {
        if (!frame.mat.empty()) {
            // Event clips are written later on another thread, so keep an
            // independent copy instead of sharing cv::Mat's reference-counted
            // image memory with the live ring buffer.
            frame.mat = frame.mat.clone();
        }
    }
    return frames;
}
