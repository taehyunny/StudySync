#pragma once

class CStudySyncClientView;

class CMainFrame : public CFrameWnd
{
public:
    CMainFrame() noexcept;
    ~CMainFrame() override = default;

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    DECLARE_MESSAGE_MAP()

private:
    CStudySyncClientView* view_ = nullptr;
};

