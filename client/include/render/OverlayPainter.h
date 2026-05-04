#pragma once

#include "model/AnalysisResult.h"

#include <d2d1.h>
#include <wrl/client.h>

class OverlayPainter {
public:
    void paint(ID2D1RenderTarget* target, const AnalysisResult& result);

private:
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
};

