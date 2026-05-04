#include "pch.h"
#include "StudySyncClientApp.h"
#include "MainFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CStudySyncClientApp, CWinApp)
END_MESSAGE_MAP()

CStudySyncClientApp theApp;

BOOL CStudySyncClientApp::InitInstance()
{
    CWinApp::InitInstance();

    auto* frame = new CMainFrame();
    if (!frame) {
        return FALSE;
    }

    m_pMainWnd = frame;
    frame->Create(nullptr, _T("StudySync"));
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();

    return TRUE;
}

