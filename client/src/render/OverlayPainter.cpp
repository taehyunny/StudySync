#include "pch.h"
#include "render/OverlayPainter.h"

void OverlayPainter::paint(ID2D1RenderTarget* target, const AnalysisResult& result)
{
    if (!target || result.landmarks.empty()) {
        return;
    }

    if (!brush_) {
        target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LimeGreen), brush_.GetAddressOf());
    }

    const auto size = target->GetSize();
    for (const auto& point : result.landmarks) {
        const D2D1_ELLIPSE dot = D2D1::Ellipse(
            D2D1::Point2F(point.x * size.width, point.y * size.height),
            3.0f,
            3.0f);
        target->FillEllipse(dot, brush_.Get());
    }
}

