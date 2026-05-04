#include "pch.h"
#include "MainFrm.h"
#include "StudySyncClientView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
END_MESSAGE_MAP()

CMainFrame::CMainFrame() noexcept
{
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    view_ = new CStudySyncClientView();
    CRect rect;
    GetClientRect(&rect);
    const CString class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW);
    view_->Create(class_name, _T("StudySyncClientView"), WS_CHILD | WS_VISIBLE, rect, this, AFX_IDW_PANE_FIRST);

    return 0;
}
