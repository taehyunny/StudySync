#include "pch.h"
#include "StudySyncClientApp.h"
#include "MainFrm.h"
#include "LoginDlg.h"
#include "network/WinHttpClient.h"
#include "network/AuthApi.h"
#include "network/TokenStore.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CStudySyncClientApp, CWinApp)
END_MESSAGE_MAP()

CStudySyncClientApp theApp;

BOOL CStudySyncClientApp::InitInstance()
{
    CWinApp::InitInstance();

    // ── HTTP 클라이언트 / AuthApi 초기화 ─────────────────────
    // 메인서버 URL: config에서 읽는 구조이지만 지금은 하드코딩
    WinHttpClient::instance().set_base_url("http://10.10.10.100:8080");

    // 저장된 토큰 복원 시도 (자동 로그인 준비)
    const std::string saved_token = TokenStore().load();
    if (!saved_token.empty()) {
        WinHttpClient::instance().set_token(saved_token);
    }

    // ── 로그인 다이얼로그 ────────────────────────────────────
    AuthApi auth_api(WinHttpClient::instance());
    CLoginDlg login_dlg(auth_api);

    if (login_dlg.DoModal() != IDOK) {
        // 취소 또는 창 닫기 → 앱 종료
        return FALSE;
    }

    // ── 로그인 성공 — JWT 주입 ───────────────────────────────
    WinHttpClient::instance().set_token(login_dlg.token());

    // ── 메인 윈도우 생성 ────────────────────────────────────
    auto* frame = new CMainFrame(login_dlg.user_id());
    if (!frame) {
        return FALSE;
    }

    m_pMainWnd = frame;
    frame->Create(nullptr, _T("StudySync"));
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();

    return TRUE;
}
