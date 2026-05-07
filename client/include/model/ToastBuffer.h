#pragma once

#include <chrono>
#include <mutex>
#include <string>

// ============================================================================
// ToastBuffer — 토스트 알림 메시지 스레드 안전 버퍼
// ============================================================================
// AlertDispatchThread (쓰기) ↔ OverlayPainter/D2DRenderer (읽기)
// post() 호출 시 duration_ms 후 자동 만료.
// ============================================================================
class ToastBuffer {
public:
    void post(const std::string& text, int duration_ms = 4000)
    {
        const long long expire = now_ms() + duration_ms;
        std::lock_guard<std::mutex> lk(mtx_);
        text_      = text;
        expire_ms_ = expire;
    }

    // 활성 메시지가 있으면 out에 담고 true 반환. 만료됐으면 false.
    bool get_active(std::string& out) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (text_.empty() || now_ms() > expire_ms_) return false;
        out = text_;
        return true;
    }

private:
    static long long now_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count();
    }

    mutable std::mutex mtx_;
    std::string        text_;
    long long          expire_ms_ = 0;
};
