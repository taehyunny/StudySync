#pragma once

#include <d2d1.h>
#include <d2d1helper.h>
#include <mutex>
#include <opencv2/core/mat.hpp>
#include <wrl/client.h>

class D2DRenderer {
public:
    ~D2DRenderer() = default;

    bool init(HWND hwnd);
    void upload_and_render(const cv::Mat& bgr);
    void notify_resize(UINT w, UINT h);

private:
    bool recreate_target();
    void apply_pending_resize();
    bool update_bgra_buffer(const cv::Mat& bgr);
    bool ensure_bitmap(UINT w, UINT h);

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory>          factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap>           bitmap_;
    D2D1_SIZE_U                                   bitmap_size_{};
    cv::Mat                                       bgra_buffer_;

    std::mutex resize_mtx_;
    UINT       pending_w_      = 0;
    UINT       pending_h_      = 0;
    bool       resize_pending_ = false;
};
