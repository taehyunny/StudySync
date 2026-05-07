#pragma once

#include "network/AuthApi.h"
#include "network/TokenStore.h"
#include "resource.h"

#include <string>

class CLoginDlg : public CDialog
{
public:
    explicit CLoginDlg(AuthApi& auth_api, CWnd* parent = nullptr);

    const std::string& token() const { return token_; }
    const std::string& name() const { return name_; }
    int user_id() const { return user_id_; }

    enum { IDD = IDD_LOGIN };

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnBtnRegister();
    DECLARE_MESSAGE_MAP()

private:
    void set_message(const std::string& msg, bool error = true);

    AuthApi& auth_api_;
    std::string token_;
    std::string name_;
    int user_id_ = 0;
};
