#include "pch.h"
#include "LoginDlg.h"
#include "RegisterDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CLoginDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_REGISTER, &CLoginDlg::OnBtnRegister)
END_MESSAGE_MAP()

CLoginDlg::CLoginDlg(AuthApi& auth_api, CWnd* parent)
    : CDialog(IDD_LOGIN, parent)
    , auth_api_(auth_api)
{
}

BOOL CLoginDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    CWnd* title = GetDlgItem(IDC_STATIC_TITLE);
    if (title) {
        LOGFONT lf{};
        GetFont()->GetLogFont(&lf);
        lf.lfHeight = -16;
        lf.lfWeight = FW_BOLD;
        auto* font = new CFont();
        font->CreateFontIndirect(&lf);
        title->SetFont(font, FALSE);
    }

    GetDlgItem(IDC_EDIT_EMAIL)->SetFocus();
    return FALSE;
}

void CLoginDlg::OnOK()
{
    CString email_cs;
    CString password_cs;
    GetDlgItemText(IDC_EDIT_EMAIL, email_cs);
    GetDlgItemText(IDC_EDIT_PASSWORD, password_cs);

    const std::string email = static_cast<LPCSTR>(CT2A(email_cs, CP_UTF8));
    const std::string password = static_cast<LPCSTR>(CT2A(password_cs, CP_UTF8));

    if (email.empty() || password.empty()) {
        set_message("Enter email and password.");
        return;
    }

    GetDlgItem(IDOK)->EnableWindow(FALSE);
    GetDlgItem(IDC_BTN_REGISTER)->EnableWindow(FALSE);
    set_message("Logging in...", false);

    LoginRequest req{ email, password };
    const AuthResponse resp = auth_api_.login(req);

    GetDlgItem(IDOK)->EnableWindow(TRUE);
    GetDlgItem(IDC_BTN_REGISTER)->EnableWindow(TRUE);

    if (!resp.success) {
        const std::string msg = resp.message.empty()
            ? "Login failed. Check your email and password."
            : resp.message;
        set_message(msg);
        GetDlgItem(IDC_EDIT_PASSWORD)->SetWindowText(_T(""));
        GetDlgItem(IDC_EDIT_PASSWORD)->SetFocus();
        return;
    }

    token_ = resp.token;
    user_id_ = resp.user_id;
    name_ = resp.message;
    TokenStore().save(resp.token);

    OutputDebugStringA("[StudySync][Auth] login succeeded\n");
    CDialog::OnOK();
}

void CLoginDlg::OnBtnRegister()
{
    CRegisterDlg dlg(auth_api_, this);
    if (dlg.DoModal() == IDOK) {
        set_message("Registration completed. Please log in.", false);
    }
}

void CLoginDlg::set_message(const std::string& msg, bool /*error*/)
{
    CString cs(CA2T(msg.c_str(), CP_UTF8));
    SetDlgItemText(IDC_STATIC_MSG, cs);

    CWnd* lbl = GetDlgItem(IDC_STATIC_MSG);
    if (lbl) {
        lbl->Invalidate();
    }
}
