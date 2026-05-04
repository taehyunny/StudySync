#include "pch.h"
#include "render/RenderThread.h"

RenderThread::RenderThread(CaptureThread::RenderFrameBuffer& frame_buffer)
    : frame_buffer_(frame_buffer)
{
}

RenderThread::~RenderThread()
{
    stop();
}

void RenderThread::start(HWND hwnd)
{
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&RenderThread::run, this, hwnd);
}

void RenderThread::stop()
{
    running_ = false;
    frame_buffer_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void RenderThread::notify_resize(UINT w, UINT h)
{
    renderer_.notify_resize(w, h);
}

void RenderThread::run(HWND hwnd)
{
    if (!renderer_.init(hwnd)) {
        running_ = false;
        return;
    }

    while (running_) {
        Frame frame = frame_buffer_.wait_pop();
        if (!running_) {
            break;
        }

        if (frame.mat.empty()) {
            continue;
        }

        renderer_.upload_and_render(frame.mat);
    }
}
