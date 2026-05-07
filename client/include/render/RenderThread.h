#pragma once

#include "capture/CaptureThread.h"
#include "model/AnalysisResultBuffer.h"
#include "model/ServerStatsSnapshot.h"
#include "model/SessionStatsHistory.h"
#include "model/ToastBuffer.h"
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

    // 세션 시작 후 호출 — D2DRenderer를 통해 OverlayPainter에 전달
    void set_session_start_ms(std::uint64_t ms)    { renderer_.set_session_start_ms(ms); }
    void set_toast_buffer(ToastBuffer* tb)          { renderer_.set_toast_buffer(tb); }
    void set_calibration_countdown(int sec)         { renderer_.set_calibration_countdown(sec); }
    void set_stats_history(SessionStatsHistory* h)  { renderer_.set_stats_history(h); }
    void set_server_stats(ServerStatsSnapshot* s)    { renderer_.set_server_stats(s); }
    void set_break_alert(std::uint64_t expire_ms)   { renderer_.set_break_alert(expire_ms); }

private:
    void run(HWND hwnd, AnalysisResultBuffer* result_buffer);

    CaptureThread::RenderFrameBuffer& frame_buffer_;
    D2DRenderer                       renderer_;
    std::atomic_bool                  running_{ false };
    std::thread                       worker_;
};
