#pragma once

#include "capture/CaptureThread.h"
#include "network/IFrameSender.h"

#include <atomic>
#include <thread>

class ZmqSendThread {
public:
    ZmqSendThread(CaptureThread::SendFrameBuffer& frame_buffer, IFrameSender& frame_sender);
    ~ZmqSendThread();

    void start(int sample_interval = 6);
    void stop();

private:
    void run(int sample_interval);

    CaptureThread::SendFrameBuffer& frame_buffer_;
    IFrameSender& frame_sender_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};
