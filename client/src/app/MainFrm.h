#pragma once

class CStudySyncClientView;

class CMainFrame : public CFrameWnd
{
public:
    explicit CMainFrame(int user_id = 0) noexcept;
    ~CMainFrame() override = default;

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnClose();   // 세션 종료 시 복기 화면 표시
    DECLARE_MESSAGE_MAP()

private:
    int                    user_id_ = 0;
    CStudySyncClientView*  view_    = nullptr;
};
