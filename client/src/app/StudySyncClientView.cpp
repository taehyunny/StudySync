#include "pch.h"
#include "StudySyncClientView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CStudySyncClientView, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
END_MESSAGE_MAP()

CStudySyncClientView::CStudySyncClientView()
    : alert_manager_(alert_queue_)
    , worker_pool_(3)
    , transport_config_()
    , transports_(make_client_transports(transport_config_))
    , capture_thread_(render_buffer_, send_buffer_, shadow_buffer_)
    , render_thread_(render_buffer_)
    , zmq_send_thread_(send_buffer_, *transports_.frame_sender)
    , zmq_recv_thread_(shadow_buffer_, event_queue_)
    , event_upload_thread_(event_queue_, *transports_.clip_store, *transports_.log_sink)
    , alert_dispatch_thread_(alert_queue_)
    , ai_heartbeat_("AI Server", transport_config_.zmq_push_endpoint)
    , main_heartbeat_("Main Server", transport_config_.jsonl_ingest_url)
    , clip_garbage_collector_(transport_config_.clip_directory, transport_config_.local_clip_retention_days)
{
}

CStudySyncClientView::~CStudySyncClientView()
{
}

int CStudySyncClientView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    capture_thread_.start(0);
    render_thread_.start(m_hWnd);
    zmq_recv_thread_.start(transport_config_.zmq_pull_endpoint);
    zmq_send_thread_.start(transport_config_.frame_sample_interval);
    event_upload_thread_.start();
    alert_dispatch_thread_.start();
    ai_heartbeat_.start([this] {
        return transports_.frame_sender && transports_.frame_sender->health_check();
    });
    main_heartbeat_.start([this] {
        return transports_.log_sink && transports_.log_sink->health_check();
    });
    clip_garbage_collector_.start();

    return 0;
}

void CStudySyncClientView::OnDestroy()
{
    clip_garbage_collector_.stop();
    main_heartbeat_.stop();
    ai_heartbeat_.stop();
    alert_dispatch_thread_.stop();
    event_upload_thread_.stop();
    zmq_recv_thread_.stop();
    zmq_send_thread_.stop();
    render_thread_.stop();
    capture_thread_.stop();
    CWnd::OnDestroy();
}

void CStudySyncClientView::OnPaint()
{
    // D2D renders directly to this HWND on the render thread.
    // Validate the paint request so GDI does not cover the D2D frame.
    ValidateRect(nullptr);
}

BOOL CStudySyncClientView::OnEraseBkgnd(CDC* /*pDC*/)
{
    // Prevent GDI background erase flicker. D2D clears the background.
    return TRUE;
}

void CStudySyncClientView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (cx > 0 && cy > 0) {
        render_thread_.notify_resize(static_cast<UINT>(cx), static_cast<UINT>(cy));
    }
}
