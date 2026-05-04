#include "pch.h"
#include "render/D2DRenderer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

bool D2DRenderer::init(HWND hwnd)
{
    hwnd_ = hwnd;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    return recreate_target();
}

bool D2DRenderer::recreate_target()
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    HRESULT hr = factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        render_target_.ReleaseAndGetAddressOf());

    bitmap_.Reset();
    return SUCCEEDED(hr);
}

void D2DRenderer::notify_resize(UINT w, UINT h)
{
    std::lock_guard<std::mutex> lock(resize_mtx_);
    pending_w_ = w;
    pending_h_ = h;
    resize_pending_ = true;
}

void D2DRenderer::apply_pending_resize()
{
    UINT w = 0;
    UINT h = 0;
    {
        std::lock_guard<std::mutex> lock(resize_mtx_);
        if (!resize_pending_) {
            return;
        }

        w = pending_w_;
        h = pending_h_;
        resize_pending_ = false;
    }

    if (render_target_ && w > 0 && h > 0) {
        render_target_->Resize(D2D1::SizeU(w, h));
        bitmap_.Reset();
    }
}

void D2DRenderer::upload_and_render(const cv::Mat& bgr)
{
    if (!render_target_ || bgr.empty()) {
        return;
    }

    apply_pending_resize();

    if (!update_bgra_buffer(bgr)) {
        return;
    }

    const UINT w = static_cast<UINT>(bgra_buffer_.cols);
    const UINT h = static_cast<UINT>(bgra_buffer_.rows);

    if (!ensure_bitmap(w, h)) {
        return;
    }

    // Reuse the existing bitmap and upload only the new pixel data.
    D2D1_RECT_U rect = D2D1::RectU(0, 0, w, h);
    if (FAILED(bitmap_->CopyFromMemory(&rect, bgra_buffer_.data, bgra_buffer_.step))) {
        return;
    }

    D2D1_SIZE_F target_size = render_target_->GetSize();
    D2D1_RECT_F dest = D2D1::RectF(0.f, 0.f, target_size.width, target_size.height);

    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    render_target_->DrawBitmap(bitmap_.Get(), dest);
    HRESULT hr = render_target_->EndDraw();

    // Recreate target resources if the graphics device is lost.
    if (hr == D2DERR_RECREATE_TARGET) {
        recreate_target();
    }
}

bool D2DRenderer::update_bgra_buffer(const cv::Mat& bgr)
{
    const int channels = bgr.channels();
    if (channels != 3 && channels != 4) {
        return false;
    }

    bgra_buffer_.create(bgr.rows, bgr.cols, CV_8UC4);

    if (channels == 3) {
        cv::cvtColor(bgr, bgra_buffer_, cv::COLOR_BGR2BGRA);
    } else {
        bgr.copyTo(bgra_buffer_);
    }

    return true;
}

bool D2DRenderer::ensure_bitmap(UINT w, UINT h)
{
    if (bitmap_ && bitmap_size_.width == w && bitmap_size_.height == h) {
        return true;
    }

    bitmap_size_ = D2D1::SizeU(w, h);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    HRESULT hr = render_target_->CreateBitmap(
        bitmap_size_,
        props,
        bitmap_.ReleaseAndGetAddressOf());

    return SUCCEEDED(hr);
}
