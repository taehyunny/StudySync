#include "pch.h"
#include "network/ZmqSendThread.h"

#include <chrono>

ZmqSendThread::ZmqSendThread(CaptureThread::SendFrameBuffer& frame_buffer, IFrameSender& frame_sender)
    : frame_buffer_(frame_buffer)
    , frame_sender_(frame_sender)
{
}

ZmqSendThread::~ZmqSendThread()
{
    stop();
}

void ZmqSendThread::start(int sample_interval)
{
    if (running_.exchange(true)) {
        return;
    }

    frame_sender_.initialize();
    worker_ = std::thread(&ZmqSendThread::run, this, sample_interval);
}

void ZmqSendThread::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    frame_sender_.shutdown();
}

void ZmqSendThread::run(int sample_interval)
{
    int frame_index = 0;
    if (sample_interval <= 0) {
        sample_interval = 1;
    }

    while (running_) {
        Frame frame;
        if (!frame_buffer_.try_pop(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if ((frame_index++ % sample_interval) != 0) {
            continue;
        }

        frame_sender_.send_frame(frame);
    }
}
