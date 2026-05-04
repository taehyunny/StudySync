#pragma once

#include "core/RingBuffer.h"
#include "event/EventShadowBuffer.h"
#include "model/Frame.h"

#include <atomic>
#include <thread>

class CaptureThread {
public:
    using RenderFrameBuffer = RingBuffer<Frame, 8>;
    using SendFrameBuffer = RingBuffer<Frame, 8>;

    CaptureThread(RenderFrameBuffer& render_buffer, SendFrameBuffer& send_buffer, EventShadowBuffer& shadow_buffer);
    ~CaptureThread();

    void start(int camera_index = 0);
    void stop();

private:
    void run(int camera_index);

    RenderFrameBuffer& render_buffer_;
    SendFrameBuffer& send_buffer_;
    EventShadowBuffer& shadow_buffer_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};
