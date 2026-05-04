#pragma once

#include "capture/CaptureThread.h"
#include "render/D2DRenderer.h"

#include <atomic>
#include <thread>

class RenderThread {
public:
    explicit RenderThread(CaptureThread::RenderFrameBuffer& frame_buffer);
    ~RenderThread();

    void start(HWND hwnd);
    void stop();
    void notify_resize(UINT w, UINT h);

private:
    void run(HWND hwnd);

    CaptureThread::RenderFrameBuffer& frame_buffer_;
    D2DRenderer                       renderer_;
    std::atomic_bool                  running_{ false };
    std::thread                       worker_;
};
