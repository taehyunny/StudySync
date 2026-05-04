#include "pch.h"
#include "event/PostureEventDetector.h"

void PostureEventDetector::set_callback(EventCallback cb)
{
    callback_ = std::move(cb);
}

void PostureEventDetector::feed(const AnalysisResult& result, const EventShadowBuffer& shadow)
{
    bad_posture_streak_ = (result.neck_angle > 25.0 || !result.posture_ok) ? bad_posture_streak_ + 1 : 0;
    drowsy_streak_ = (result.ear < 0.25 || result.drowsy) ? drowsy_streak_ + 1 : 0;

    if (event_cooldown_) {
        if (bad_posture_streak_ == 0 && drowsy_streak_ == 0) {
            event_cooldown_ = false;
        }
        return;
    }

    if (bad_posture_streak_ >= 5) {
        emit_event(PostureEventType::BadPosture, "neck_angle over threshold", result, shadow);
        return;
    }

    if (drowsy_streak_ >= 5) {
        emit_event(PostureEventType::Drowsy, "EAR below threshold", result, shadow);
    }
}

void PostureEventDetector::reset_cooldown()
{
    event_cooldown_ = false;
}

void PostureEventDetector::emit_event(PostureEventType type, const char* reason, const AnalysisResult& result, const EventShadowBuffer& shadow)
{
    event_cooldown_ = true;

    if (!callback_) {
        return;
    }

    PostureEvent event;
    event.type = type;
    event.timestamp_ms = result.timestamp_ms;
    event.reason = reason;
    event.frames = shadow.snapshot(30);
    callback_(std::move(event));
}

