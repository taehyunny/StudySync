#include "pch.h"
#include "StudySyncClientView.h"
#include "network/WinHttpClient.h"

#include <windows.h>
#include <sstream>

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

// ── 내부 유틸 ──────────────────────────────────────────────────
namespace {
std::string current_iso8601()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+09:00",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return buf;
}
} // namespace

// ── 생성 ───────────────────────────────────────────────────────

CStudySyncClientView::CStudySyncClientView()
    : alert_manager_(alert_queue_)
    , worker_pool_(3)
    , transport_config_()
    , transports_(make_client_transports(transport_config_))
    , capture_thread_(render_buffer_, send_buffer_, shadow_buffer_)
    , render_thread_(render_buffer_)
    , ai_tcp_client_(send_buffer_, shadow_buffer_, event_queue_, result_buffer_, transport_config_.jpeg_quality)
    , event_upload_thread_(event_queue_, *transports_.clip_store, *transports_.log_sink)
    , alert_dispatch_thread_(alert_queue_)
    , ai_heartbeat_("AI Server",    transport_config_.ai_server_host + ":9100")
    , main_heartbeat_("Main Server", transport_config_.main_server_url)
    , clip_garbage_collector_(transport_config_.clip_directory,
                              transport_config_.local_clip_retention_days)
{
}

CStudySyncClientView::~CStudySyncClientView()
{
}

// ── 공개 인터페이스 ────────────────────────────────────────────

void CStudySyncClientView::set_session_id(long long session_id,
                                          const std::string& start_time)
{
    session_id_         = session_id;
    session_start_time_ = start_time;
}

// ── 윈도우 메시지 ──────────────────────────────────────────────

int CStudySyncClientView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;

    capture_thread_.start(0, transport_config_.capture_fps);
    render_thread_.start(m_hWnd, result_buffer_);

    ai_tcp_client_.start(
        transport_config_.ai_server_host,
        transport_config_.ai_server_port,
        session_id_,
        transport_config_.frame_sample_interval);

    event_upload_thread_.start();
    alert_dispatch_thread_.start();

    ai_heartbeat_.start([this] {
        return ai_tcp_client_.is_connected();
    });
    main_heartbeat_.start([this] {
        return transports_.log_sink && transports_.log_sink->health_check();
    });

    clip_garbage_collector_.start();
    return 0;
}

void CStudySyncClientView::OnDestroy()
{
    // ── 세션 종료 먼저 (스레드 종료 전에 호출) ──────────────
    if (session_id_ > 0) {
        const std::string end_time = current_iso8601();
        SessionApi session_api(WinHttpClient::instance());
        const SessionEndResult result = session_api.end(session_id_, end_time);

        if (result.success) {
            // 종료 통계 디버그 출력 (추후 UI 표시 가능)
            std::ostringstream msg;
            msg << "[StudySync] 세션 종료 — "
                << "집중시간: " << result.focus_min << "분, "
                << "평균집중도: " << static_cast<int>(result.avg_focus * 100) << "%, "
                << "목표달성: " << (result.goal_achieved ? "Y" : "N") << "\n";
            OutputDebugStringA(msg.str().c_str());
        }
        session_id_ = 0;
    }

    // ── 스레드 종료 (시작 역순) ──────────────────────────────
    clip_garbage_collector_.stop();
    main_heartbeat_.stop();
    ai_heartbeat_.stop();
    alert_dispatch_thread_.stop();
    event_upload_thread_.stop();
    ai_tcp_client_.stop();
    render_thread_.stop();
    capture_thread_.stop();

    CWnd::OnDestroy();
}

void CStudySyncClientView::OnPaint()
{
    ValidateRect(nullptr);
}

BOOL CStudySyncClientView::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;
}

void CStudySyncClientView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (cx > 0 && cy > 0) {
        render_thread_.notify_resize(static_cast<UINT>(cx), static_cast<UINT>(cy));
    }
}
