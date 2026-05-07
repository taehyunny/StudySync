#pragma once

#include <cstdint>
#include <mutex>

// Thread-safe cache for server-side statistics fetched from /stats/today.
// Writer: background HTTP worker. Reader: Direct2D render thread.
class ServerStatsSnapshot {
public:
    struct Data {
        bool valid = false;
        int focus_min = 0;
        int break_min = 0;
        int warning_count = 0;
        double avg_focus = 0.0; // 0.0 ~ 1.0 or 0.0 ~ 100.0 normalized on update.
        std::uint64_t fetched_at_ms = 0;
    };

    void update(Data data)
    {
        if (data.avg_focus > 1.0) {
            data.avg_focus /= 100.0;
        }
        data.valid = true;

        std::lock_guard<std::mutex> lock(mtx_);
        data_ = data;
    }

    Data snapshot() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_;
    }

private:
    mutable std::mutex mtx_;
    Data data_;
};
