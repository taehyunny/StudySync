#pragma once

#include "event/EventShadowBuffer.h"
#include "model/AnalysisResult.h"
#include "model/PostureEvent.h"

#include <functional>

class PostureEventDetector {
public:
    using EventCallback = std::function<void(PostureEvent)>;

    void set_callback(EventCallback cb);
    void feed(const AnalysisResult& result, const EventShadowBuffer& shadow);
    void reset_cooldown();

private:
    void emit_event(PostureEventType type, const char* reason, const AnalysisResult& result, const EventShadowBuffer& shadow);

    EventCallback callback_;
    int bad_posture_streak_ = 0;
    int drowsy_streak_ = 0;
    bool event_cooldown_ = false;
};

