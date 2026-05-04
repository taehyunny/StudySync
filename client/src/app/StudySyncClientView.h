#pragma once

#include "alert/AlertDispatchThread.h"
#include "alert/AlertManager.h"
#include "capture/CaptureThread.h"
#include "core/WorkerThreadPool.h"
#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "network/ZmqRecvThread.h"
#include "network/ZmqSendThread.h"
#include "network/EventUploadThread.h"
#include "network/ClientTransportConfig.h"
#include "network/ClientTransportFactory.h"
#include "render/RenderThread.h"

class CStudySyncClientView : public CWnd
{
public:
    CStudySyncClientView();
    ~CStudySyncClientView() override;

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    DECLARE_MESSAGE_MAP()

private:
    CaptureThread::RenderFrameBuffer render_buffer_;
    CaptureThread::SendFrameBuffer   send_buffer_;
    EventShadowBuffer                shadow_buffer_;
    EventQueue                       event_queue_;
    AlertQueue                       alert_queue_;
    AlertManager                     alert_manager_;
    WorkerThreadPool                 worker_pool_;
    ClientTransportConfig            transport_config_;
    ClientTransports                 transports_;
    CaptureThread                    capture_thread_;
    RenderThread                     render_thread_;
    ZmqSendThread                    zmq_send_thread_;
    ZmqRecvThread                    zmq_recv_thread_;
    EventUploadThread                event_upload_thread_;
    AlertDispatchThread              alert_dispatch_thread_;
};
