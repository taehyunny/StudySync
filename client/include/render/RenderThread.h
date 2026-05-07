#pragma once

#include "capture/CaptureThread.h"
#include "model/AnalysisResultBuffer.h"
#include "render/D2DRenderer.h"

#include <atomic>
#include <thread>

class RenderThread {
public:
    explicit RenderThread(CaptureThread::RenderFrameBuffer& frame_buffer);
    ~RenderThread();

    void start(HWND hwnd, AnalysisResultBuffer& result_buffer);
    void stop();
    void notify_resize(UINT w, UINT h);

private:
    void run(HWND hwnd, AnalysisResultBuffer* result_buffer);

    CaptureThread::RenderFrameBuffer& frame_buffer_;
    D2DRenderer                       renderer_;
    std::atomic_bool                  running_{ false };
    std::thread                       worker_;
};
