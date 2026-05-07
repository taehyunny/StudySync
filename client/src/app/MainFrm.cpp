#include "pch.h"
#include "MainFrm.h"
#include "StudySyncClientView.h"
#include "network/SessionApi.h"
#include "network/WinHttpClient.h"

#include <sstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
END_MESSAGE_MAP()

CMainFrame::CMainFrame(int user_id) noexcept
    : user_id_(user_id)
{
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    // ── 뷰 생성 ──────────────────────────────────────────────
    view_ = new CStudySyncClientView();
    CRect rect;
    GetClientRect(&rect);
    const CString class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW);
    view_->Create(class_name, _T("StudySyncClientView"),
                  WS_CHILD | WS_VISIBLE, rect, this, AFX_IDW_PANE_FIRST);

    // ── 세션 시작 ────────────────────────────────────────────
    // 현재 시각을 ISO8601로 구성하여 메인서버에 POST /session/start 전송
    SYSTEMTIME st;
    GetLocalTime(&st);
    char iso[32];
    snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d+09:00",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    SessionApi session_api(WinHttpClient::instance());
    const SessionStartResult result = session_api.start(iso);

    if (result.success && result.session_id > 0) {
        view_->set_session_id(result.session_id, iso);
    } else {
        // 세션 시작 실패 — 오프라인 모드로 진행 (session_id = 0)
        // AI서버로의 프레임 전송은 동작하나 메인서버 DB에 기록 안 됨
        OutputDebugStringA("[StudySync] session/start failed — running offline\n");
    }

    return 0;
}
