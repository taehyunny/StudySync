#include "pch.h"
#include "RegisterDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CRegisterDlg, CDialog)
END_MESSAGE_MAP()

CRegisterDlg::CRegisterDlg(AuthApi& auth_api, CWnd* parent)
    : CDialog(IDD_REGISTER, parent)
    , auth_api_(auth_api)
{
}

BOOL CRegisterDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    GetDlgItem(IDC_EDIT_NAME)->SetFocus();
    return FALSE;
}

void CRegisterDlg::OnOK()
{
    // ── 입력 값 수집 ─────────────────────────────────────────
    CString name_cs, email_cs, pw_cs, pw2_cs;
    GetDlgItemText(IDC_EDIT_NAME,             name_cs);
    GetDlgItemText(IDC_EDIT_EMAIL2,           email_cs);
    GetDlgItemText(IDC_EDIT_PASSWORD2,        pw_cs);
    GetDlgItemText(IDC_EDIT_PASSWORD_CONFIRM, pw2_cs);

    const std::string name     = static_cast<LPCSTR>(CT2A(name_cs,  CP_UTF8));
    const std::string email    = static_cast<LPCSTR>(CT2A(email_cs, CP_UTF8));
    const std::string password = static_cast<LPCSTR>(CT2A(pw_cs,    CP_UTF8));
    const std::string confirm  = static_cast<LPCSTR>(CT2A(pw2_cs,   CP_UTF8));

    // ── 입력 검증 ────────────────────────────────────────────
    if (name.empty() || email.empty() || password.empty()) {
        set_message("모든 항목을 입력해 주세요.");
        return;
    }

    if (password != confirm) {
        set_message("비밀번호가 일치하지 않습니다.");
        GetDlgItem(IDC_EDIT_PASSWORD_CONFIRM)->SetWindowText(_T(""));
        GetDlgItem(IDC_EDIT_PASSWORD_CONFIRM)->SetFocus();
        return;
    }

    if (password.size() < 6) {
        set_message("비밀번호는 6자 이상이어야 합니다.");
        return;
    }

    // ── 서버 요청 ────────────────────────────────────────────
    GetDlgItem(IDOK)->EnableWindow(FALSE);
    set_message("가입 처리 중...", false);

    RegisterRequest req{ email, password, name };
    const AuthResponse resp = auth_api_.register_user(req);

    GetDlgItem(IDOK)->EnableWindow(TRUE);

    if (!resp.success) {
        const std::string msg = resp.message.empty()
            ? "회원가입에 실패했습니다. 이메일을 확인해 주세요."
            : resp.message;
        set_message(msg);
        return;
    }

    CDialog::OnOK();
}

void CRegisterDlg::set_message(const std::string& msg, bool /*error*/)
{
    CString cs(CA2T(msg.c_str(), CP_UTF8));
    SetDlgItemText(IDC_STATIC_MSG2, cs);
    CWnd* lbl = GetDlgItem(IDC_STATIC_MSG2);
    if (lbl) lbl->Invalidate();
}
