#pragma once

#include "model/AnalysisResult.h"

#include <deque>
#include <mutex>

// ============================================================================
// SessionStatsHistory — 세션 전체 분석 결과 이력 (링버퍼 + 누적 통계)
// ============================================================================
// result_callback 스레드(쓰기) / RenderThread(읽기) 공유
// ============================================================================
class SessionStatsHistory {
public:
    static constexpr int kChartSamples = 150;  // 약 30초 분량 (5fps 기준)

    void push(const AnalysisResult& r)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        chart_.push_back(r.focus_score);
        if (static_cast<int>(chart_.size()) > kChartSamples)
            chart_.pop_front();

        total_++;
        focus_sum_     += r.focus_score;
        if (!r.posture_ok) ++posture_bad_;
        if (r.drowsy)      ++drowsy_;
        if (r.absent)      ++absent_;
    }

    // ── 렌더 스레드용 스냅샷 (복사본 반환) ─────────────────────
    struct Snapshot {
        std::deque<int> chart;      // 꺾은선 그래프용 포커스 이력
        int   avg_focus     = 0;    // 세션 평균 집중도 (0-100)
        float posture_ok_pct= 0.0f; // 자세 양호 비율 (0-1)
        int   event_count   = 0;    // 졸음+자세불량 이벤트 합산
        int   total         = 0;    // 전체 샘플 수
    };

    Snapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        Snapshot s;
        s.chart     = chart_;
        s.total     = total_;
        s.avg_focus = (total_ > 0)
                      ? static_cast<int>(focus_sum_ / total_)
                      : 0;
        s.posture_ok_pct = (total_ > 0)
                           ? 1.0f - static_cast<float>(posture_bad_) / total_
                           : 1.0f;
        s.event_count = drowsy_ + posture_bad_;
        return s;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        chart_.clear();
        total_ = focus_sum_ = posture_bad_ = drowsy_ = absent_ = 0;
    }

private:
    mutable std::mutex mtx_;
    std::deque<int>    chart_;
    long long          total_       = 0;
    long long          focus_sum_   = 0;
    int                posture_bad_ = 0;
    int                drowsy_      = 0;
    int                absent_      = 0;
};
