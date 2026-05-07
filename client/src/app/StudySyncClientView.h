#pragma once

#include "alert/AlertDispatchThread.h"
#include "alert/AlertManager.h"
#include "capture/CaptureThread.h"
#include "core/WorkerThreadPool.h"
#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "model/AnalysisResultBuffer.h"
#include "network/AiAnalyzeApi.h"
#include "network/AiAnalyzeThread.h"
#include "network/ClientTransportConfig.h"
#include "network/ClientTransportFactory.h"
#include "network/EventUploadThread.h"
#include "network/HeartbeatClient.h"
#include "network/LocalClipGarbageCollector.h"
#include "network/SessionApi.h"
#include "render/RenderThread.h"

#include <string>

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
    DECLARE_MESSAGE_MAP()

private:
    CaptureThread::RenderFrameBuffer render_buffer_;
    CaptureThread::SendFrameBuffer send_buffer_;
    EventShadowBuffer shadow_buffer_;
    EventQueue event_queue_;
    AlertQueue alert_queue_;
    AnalysisResultBuffer result_buffer_;

    ClientTransportConfig transport_config_;
    ClientTransports transports_;
    long long session_id_ = 0;
    std::string session_start_time_;

    AlertManager alert_manager_;
    WorkerThreadPool worker_pool_;
    AiAnalyzeApi ai_api_;
    CaptureThread capture_thread_;
    RenderThread render_thread_;
    AiAnalyzeThread ai_analyze_thread_;
    EventUploadThread event_upload_thread_;
    AlertDispatchThread alert_dispatch_thread_;
    HeartbeatClient ai_heartbeat_;
    HeartbeatClient main_heartbeat_;
    LocalClipGarbageCollector clip_garbage_collector_;
};
