#include "pch.h"
#include "render/OverlayPainter.h"

#include <algorithm>
#include <chrono>
#include <cwchar>

#pragma comment(lib, "dwrite.lib")

namespace {
constexpr float kPanelW = 215.0f;
constexpr float kPanelH = 178.0f;
constexpr float kPanelMargin = 12.0f;
constexpr float kCornerR = 8.0f;
constexpr float kRowH = 28.0f;
constexpr float kPadX = 12.0f;
constexpr float kPadY = 10.0f;

std::wstring widen_state(const AnalysisResult& result)
{
    if (result.absent) return L"Absent";
    if (result.drowsy) return L"Drowsy";
    if (result.state == "distracted") return L"Distracted";
    if (result.state == "drowsy") return L"Drowsy";
    if (result.state == "absent") return L"Absent";
    if (result.state == "warning") return L"Warning";
    return L"Focus";
}
} // namespace

OverlayPainter::~OverlayPainter()
{
    release_resources();
}

void OverlayPainter::invalidate()
{
    release_resources();
    initialized_ = false;
}

void OverlayPainter::release_resources()
{
    brush_bg_.Reset();
    brush_white_.Reset();
    brush_gray_.Reset();
    brush_green_.Reset();
    brush_yellow_.Reset();
    brush_orange_.Reset();
    brush_red_.Reset();
    brush_bar_bg_.Reset();
    brush_toast_bg_.Reset();
    brush_timer_bg_.Reset();
    fmt_label_.Reset();
    fmt_value_.Reset();
    fmt_score_.Reset();
}

bool OverlayPainter::ensure_resources(ID2D1RenderTarget* rt)
{
    if (initialized_) return true;
    if (!rt) return false;

    if (!dwrite_) {
        const HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
        if (FAILED(hr)) return false;
    }

    const WCHAR* font = L"Segoe UI";
    dwrite_->CreateTextFormat(font, nullptr,
                              DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL,
                              11.0f,
                              L"en-US",
                              fmt_label_.GetAddressOf());

    dwrite_->CreateTextFormat(font, nullptr,
                              DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL,
                              12.0f,
                              L"en-US",
                              fmt_value_.GetAddressOf());

    dwrite_->CreateTextFormat(font, nullptr,
                              DWRITE_FONT_WEIGHT_BOLD,
                              DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL,
                              22.0f,
                              L"en-US",
                              fmt_score_.GetAddressOf());

    if (!fmt_label_ || !fmt_value_ || !fmt_score_) return false;

    fmt_label_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt_value_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt_score_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Brushes are render-target resources, so they must be rebuilt after target recreation.
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f), brush_bg_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_white_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f), brush_gray_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.72f, 0.50f), brush_green_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.62f, 0.04f), brush_yellow_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.45f, 0.12f), brush_orange_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.20f, 0.20f), brush_red_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.22f), brush_bar_bg_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.12f, 0.12f, 0.88f), brush_toast_bg_.GetAddressOf()))) return false;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0.0f,  0.0f,  0.0f,  0.60f), brush_timer_bg_.GetAddressOf()))) return false;

    initialized_ = true;
    return true;
}

void OverlayPainter::draw(ID2D1RenderTarget* rt, const AnalysisResult& result)
{
    if (!rt) return;
    if (!ensure_resources(rt)) return;

    // ── 좌상단: 세션 타이머 (항상 표시) ──────────────────────
    constexpr float kLeft = 12.0f;
    float next_y = 12.0f;

    const std::uint64_t start_ms = session_start_ms_.load();
    if (start_ms > 0) {
        draw_timer(rt, kLeft, next_y);
        next_y += 36.0f;
    }

    // ── 좌상단: 토스트 알림 (활성 시 표시) ────────────────────
    if (toast_buffer_) {
        std::string toast_text;
        if (toast_buffer_->get_active(toast_text)) {
            draw_toast(rt, kLeft, next_y, toast_text);
        }
    }

    // ── 우상단: 분석 수치 패널 ─────────────────────────────────
    const D2D1_SIZE_F size = rt->GetSize();
    const float px = size.width - kPanelW - kPanelMargin;
    const float py = kPanelMargin;
    const D2D1_RECT_F panel = D2D1::RectF(px, py, px + kPanelW, py + kPanelH);

    draw_panel_bg(rt, panel);

    float y = py + kPadY;

    draw_text(rt, L"Focus", px + kPadX, y, 50, 16, fmt_label_.Get(), brush_gray_.Get());
    {
        wchar_t value[16]{};
        swprintf_s(value, L"%d", result.focus_score);
        rt->DrawText(value,
                     static_cast<UINT32>(wcslen(value)),
                     fmt_score_.Get(),
                     D2D1::RectF(px + kPanelW - 50, y - 4, px + kPanelW - kPadX, y + 24),
                     brush_white_.Get());

        const float ratio = std::clamp(static_cast<float>(result.focus_score) / 100.0f, 0.0f, 1.0f);
        draw_bar(rt, px + kPadX + 52, y + 4, kPanelW - kPadX * 2 - 96, 10,
                 ratio, focus_bar_brush(static_cast<float>(result.focus_score)), brush_bar_bg_.Get());
    }
    y += kRowH;

    draw_text(rt, L"State", px + kPadX, y, 42, 16, fmt_label_.Get(), brush_gray_.Get());
    {
        ID2D1SolidColorBrush* dot = brush_green_.Get();
        if (result.absent || !result.posture_ok) dot = brush_red_.Get();
        else if (result.drowsy) dot = brush_orange_.Get();
        else if (result.state == "distracted" || result.state == "warning") dot = brush_yellow_.Get();

        draw_dot(rt, px + kPadX + 48, y + 8, 5.0f, dot);
        draw_text(rt, widen_state(result), px + kPadX + 60, y, 100, 16, fmt_value_.Get(), brush_white_.Get());
    }
    y += kRowH;

    draw_text(rt, L"Posture", px + kPadX, y, 50, 16, fmt_label_.Get(), brush_gray_.Get());
    draw_text(rt, result.posture_ok ? L"OK" : L"Warning",
              px + kPadX + 60, y, 90, 16, fmt_value_.Get(),
              result.posture_ok ? brush_green_.Get() : brush_red_.Get());
    y += kRowH;

    draw_text(rt, L"Neck", px + kPadX, y, 50, 16, fmt_label_.Get(), brush_gray_.Get());
    {
        wchar_t value[24]{};
        swprintf_s(value, L"%.1f deg", result.neck_angle);
        ID2D1SolidColorBrush* brush = (result.neck_angle > 30.0) ? brush_red_.Get()
                                      : (result.neck_angle > 15.0) ? brush_yellow_.Get()
                                                                   : brush_white_.Get();
        draw_text(rt, value, px + kPadX + 60, y, 100, 16, fmt_value_.Get(), brush);
    }
    y += kRowH;

    draw_text(rt, L"EAR", px + kPadX, y, 30, 16, fmt_label_.Get(), brush_gray_.Get());
    {
        const float ratio = std::clamp(static_cast<float>(result.ear) / 0.40f, 0.0f, 1.0f);
        ID2D1SolidColorBrush* fill = (result.ear < 0.20) ? brush_red_.Get()
                                    : (result.ear < 0.25) ? brush_orange_.Get()
                                                          : brush_green_.Get();
        draw_bar(rt, px + kPadX + 36, y + 4, kPanelW - kPadX * 2 - 80, 10, ratio, fill, brush_bar_bg_.Get());
        wchar_t value[16]{};
        swprintf_s(value, L"%.2f", result.ear);
        draw_text(rt, value, px + kPanelW - 50, y, 38, 16, fmt_label_.Get(), brush_gray_.Get());
    }
    y += kRowH;

    draw_text(rt, L"Shoulder", px + kPadX, y, 56, 16, fmt_label_.Get(), brush_gray_.Get());
    {
        wchar_t value[24]{};
        swprintf_s(value, L"%.1f deg", result.shoulder_diff);
        ID2D1SolidColorBrush* brush = (result.shoulder_diff > 20.0) ? brush_red_.Get()
                                      : (result.shoulder_diff > 10.0) ? brush_yellow_.Get()
                                                                      : brush_white_.Get();
        draw_text(rt, value, px + kPadX + 70, y, 90, 16, fmt_value_.Get(), brush);
    }
}

void OverlayPainter::draw_panel_bg(ID2D1RenderTarget* rt, const D2D1_RECT_F& panel)
{
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(panel, kCornerR, kCornerR);
    rt->FillRoundedRectangle(rr, brush_bg_.Get());
    rt->DrawRoundedRectangle(rr, brush_gray_.Get(), 0.8f);
}

void OverlayPainter::draw_bar(ID2D1RenderTarget* rt,
                              float x,
                              float y,
                              float w,
                              float h,
                              float ratio,
                              ID2D1SolidColorBrush* fill,
                              ID2D1SolidColorBrush* bg)
{
    ratio = std::clamp(ratio, 0.0f, 1.0f);

    const D2D1_ROUNDED_RECT bg_rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), h / 2.0f, h / 2.0f);
    rt->FillRoundedRectangle(bg_rr, bg);

    if (ratio <= 0.0f) return;

    const float raw_fill_w = w * ratio;
    const float fill_w = raw_fill_w < h ? h : raw_fill_w;
    const D2D1_ROUNDED_RECT fill_rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + fill_w, y + h), h / 2.0f, h / 2.0f);
    rt->FillRoundedRectangle(fill_rr, fill);
}

void OverlayPainter::draw_text(ID2D1RenderTarget* rt,
                               const std::wstring& text,
                               float x,
                               float y,
                               float w,
                               float h,
                               IDWriteTextFormat* fmt,
                               ID2D1SolidColorBrush* brush)
{
    rt->DrawText(text.c_str(),
                 static_cast<UINT32>(text.size()),
                 fmt,
                 D2D1::RectF(x, y, x + w, y + h),
                 brush);
}

void OverlayPainter::draw_dot(ID2D1RenderTarget* rt,
                              float cx,
                              float cy,
                              float r,
                              ID2D1SolidColorBrush* brush)
{
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush);
}

ID2D1SolidColorBrush* OverlayPainter::focus_bar_brush(float score) const
{
    if (score >= 70.0f) return brush_green_.Get();
    if (score >= 40.0f) return brush_yellow_.Get();
    return brush_red_.Get();
}

void OverlayPainter::draw_timer(ID2D1RenderTarget* rt, float x, float y)
{
    using namespace std::chrono;
    const long long now = duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
    const long long elapsed_s = (now - static_cast<long long>(session_start_ms_.load())) / 1000;
    const long long h = elapsed_s / 3600;
    const long long m = (elapsed_s % 3600) / 60;
    const long long s = elapsed_s % 60;

    wchar_t buf[32];
    swprintf_s(buf, L"%02lld:%02lld:%02lld", h, m, s);

    constexpr float kW = 90.0f;
    constexpr float kH = 26.0f;
    const D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(x, y, x + kW, y + kH), 5.0f, 5.0f);
    rt->FillRoundedRectangle(bg, brush_timer_bg_.Get());

    draw_text(rt, buf, x + 8.0f, y + 5.0f, kW - 10.0f, kH, fmt_value_.Get(), brush_white_.Get());
}

void OverlayPainter::draw_toast(ID2D1RenderTarget* rt, float x, float y, const std::string& text)
{
    // ASCII 변환 (DirectWrite는 wstring)
    const std::wstring wtext(text.begin(), text.end());

    constexpr float kW = 240.0f;
    constexpr float kH = 48.0f;

    const D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(x, y, x + kW, y + kH), 6.0f, 6.0f);
    rt->FillRoundedRectangle(bg, brush_toast_bg_.Get());
    rt->DrawRoundedRectangle(bg, brush_red_.Get(), 1.0f);

    // 경고 아이콘 (느낌표)
    draw_text(rt, L"!", x + 10.0f, y + 8.0f, 16.0f, 32.0f, fmt_score_.Get(), brush_white_.Get());

    // 알림 텍스트 (title : message 구조, ':' 기준으로 두 줄로 분리)
    const auto colon = text.find(": ");
    if (colon != std::string::npos) {
        const std::wstring title(text.begin(), text.begin() + colon);
        const std::wstring msg(text.begin() + colon + 2, text.end());
        draw_text(rt, title, x + 30.0f, y +  8.0f, kW - 40.0f, 18.0f, fmt_value_.Get(), brush_white_.Get());
        draw_text(rt, msg,   x + 30.0f, y + 27.0f, kW - 40.0f, 16.0f, fmt_label_.Get(), brush_gray_.Get());
    } else {
        draw_text(rt, wtext, x + 30.0f, y + 14.0f, kW - 40.0f, 20.0f, fmt_value_.Get(), brush_white_.Get());
    }
}
