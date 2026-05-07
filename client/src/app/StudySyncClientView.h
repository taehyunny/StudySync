#pragma once

#include "alert/AlertDispatchThread.h"
#include "alert/AlertManager.h"
#include "analysis/DummyAnalysisGenerator.h"
#include "model/ToastBuffer.h"
#include "capture/CaptureThread.h"
#include "core/WorkerThreadPool.h"
#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "model/AnalysisResultBuffer.h"
#include "network/AiTcpClient.h"
#include "network/ClientTransportConfig.h"
#include "network/ClientTransportFactory.h"
#include "network/EventUploadThread.h"
#include "network/HeartbeatClient.h"
#include "network/LocalClipGarbageCollector.h"
#include "network/SessionApi.h"
#include "render/RenderThread.h"

#include <mutex>
#include <string>
#include <vector>

class CStudySyncClientView : public CWnd
{
public:
    CStudySyncClientView();
    ~CStudySyncClientView() override;

    // Stores the active study session id after login/start-session succeeds.
    void set_session_id(long long session_id, const std::string& start_time);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    DECLARE_MESSAGE_MAP()

    // ── 캘리브레이션 ──────────────────────────────────────────────
    static constexpr UINT_PTR IDT_CALIB      = 1;  // 1초 카운트다운 타이머
    static constexpr UINT_PTR IDT_CALIB_HIDE = 2;  // 완료 후 1.5초 뒤 오버레이 숨김
    static constexpr UINT_PTR IDT_LOG_FLUSH  = 3;  // 분석 데이터 주기적 서버 전송

    void begin_calibration();
    void finish_calibration();

    std::mutex              calib_mtx_;
    std::vector<double>     calib_samples_;
    bool                    calibrating_    = false;
    int                     calib_tick_     = 0;    // 남은 초

private:
    CaptureThread::RenderFrameBuffer render_buffer_;
    CaptureThread::SendFrameBuffer send_buffer_;
    EventShadowBuffer shadow_buffer_;
    EventQueue event_queue_;
    AlertQueue alert_queue_;
    AnalysisResultBuffer result_buffer_;
    ToastBuffer toast_buffer_;              // AlertDispatchThread(쓰기) ↔ OverlayPainter(읽기)

    ClientTransportConfig transport_config_;
    ClientTransports transports_;
    long long session_id_ = 0;
    std::string session_start_time_;
    std::uint64_t session_start_steady_ms_ = 0;  // 세션 HUD 타이머용

    AlertManager alert_manager_;
    WorkerThreadPool worker_pool_;
    CaptureThread capture_thread_;
    RenderThread render_thread_;
    AiTcpClient ai_tcp_client_;
    DummyAnalysisGenerator dummy_generator_;
    EventUploadThread event_upload_thread_;
    AlertDispatchThread alert_dispatch_thread_;  // toast_buffer_ 선언 이후 초기화
    HeartbeatClient ai_heartbeat_;
    HeartbeatClient main_heartbeat_;
    LocalClipGarbageCollector clip_garbage_collector_;
};
