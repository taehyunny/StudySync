#pragma once

#include "resource.h"

class CStudySyncClientApp : public CWinApp
{
public:
    BOOL InitInstance() override;

    DECLARE_MESSAGE_MAP()
};

extern CStudySyncClientApp theApp;

