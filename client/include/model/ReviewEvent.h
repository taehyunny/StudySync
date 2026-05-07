#pragma once

#include "model/PostureEvent.h"

#include <cstdint>
#include <string>

// 복기 화면에서 한 행을 표현하는 뷰모델
struct ReviewEvent {
    std::string       event_id;
    std::uint64_t     timestamp_ms = 0;
    PostureEventType  type         = PostureEventType::BadPosture;
    double            confidence   = 1.0;
    std::string       clip_dir;      // 로컬 클립 디렉터리 경로

    // 피드백 상태
    enum class Feedback { None, Correct, Wrong };
    Feedback feedback = Feedback::None;

    // confidence 기준 표시 우선순위
    // 0: confidence < 0.70  (최우선 — 상단 정렬, 강조)
    // 1: confidence < 0.85  (일반 피드백 표시)
    // 2: confidence >= 0.85 (피드백 버튼 숨김)
    int priority() const {
        if (confidence < 0.70) return 0;
        if (confidence < 0.85) return 1;
        return 2;
    }

    bool needs_feedback() const { return confidence < 0.85; }
};
