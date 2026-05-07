#include "pch.h"
#include "MainFrm.h"
#include "ReviewDlg.h"
#include "StudySyncClientView.h"
#include "network/SessionApi.h"
#include "network/WinHttpClient.h"

#include <sstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_CLOSE()
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
        OutputDebugStringA("[StudySync] session/start failed — running offline\n");
    }

    return 0;
}

// ── 세션 종료 — 복기 화면 표시 후 창 닫기 ─────────────────────────

void CMainFrame::OnClose()
{
    // View가 살아있고, 복기 대상 이벤트가 있으면 ReviewDlg를 먼저 열기
    if (view_) {
        ReviewEventStore& store = view_->review_store();
        if (store.count_uncertain() > 0) {
            ReviewDlg dlg(store, this);
            dlg.DoModal();
        }
    }

    // 뷰 소멸 → OnDestroy → 세션 종료 API, 스레드 정리
    CFrameWnd::OnClose();
}
