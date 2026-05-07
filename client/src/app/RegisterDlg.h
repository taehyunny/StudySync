#pragma once

#include "network/AuthApi.h"
#include "resource.h"

#include <string>

// ============================================================================
// CRegisterDlg — 회원가입 다이얼로그
// ============================================================================
// DoModal() == IDOK 이면 가입 성공.
// ============================================================================
class CRegisterDlg : public CDialog
{
public:
    explicit CRegisterDlg(AuthApi& auth_api, CWnd* parent = nullptr);

    enum { IDD = IDD_REGISTER };

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    DECLARE_MESSAGE_MAP()

private:
    void set_message(const std::string& msg, bool error = true);

    AuthApi& auth_api_;
};
