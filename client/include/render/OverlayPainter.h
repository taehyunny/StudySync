#pragma once

#include "model/AnalysisResult.h"

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <wrl/client.h>

// Draws posture/focus HUD data on top of the current Direct2D frame.
// The renderer owns frame upload; this class owns only overlay resources and drawing.
class OverlayPainter {
public:
    ~OverlayPainter();

    // Called when the render target is recreated so D2D target-bound resources are rebuilt.
    void invalidate();

    // Draws the latest AI analysis result over the camera frame.
    void draw(ID2D1RenderTarget* rt, const AnalysisResult& result);

private:
    bool ensure_resources(ID2D1RenderTarget* rt);
    void release_resources();

    void draw_panel_bg(ID2D1RenderTarget* rt, const D2D1_RECT_F& panel);
    void draw_bar(ID2D1RenderTarget* rt,
                  float x,
                  float y,
                  float w,
                  float h,
                  float ratio,
                  ID2D1SolidColorBrush* fill,
                  ID2D1SolidColorBrush* bg);
    void draw_text(ID2D1RenderTarget* rt,
                   const std::wstring& text,
                   float x,
                   float y,
                   float w,
                   float h,
                   IDWriteTextFormat* fmt,
                   ID2D1SolidColorBrush* brush);
    void draw_dot(ID2D1RenderTarget* rt,
                  float cx,
                  float cy,
                  float r,
                  ID2D1SolidColorBrush* brush);

    ID2D1SolidColorBrush* focus_bar_brush(float score) const;

    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt_label_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt_value_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt_score_;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_bg_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_white_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_gray_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_green_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_yellow_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_orange_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_red_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_bar_bg_;

    bool initialized_ = false;
};
