#pragma once

#include "model/AnalysisResult.h"
#include "model/PostureEvent.h"
#include "network/IEventClipStore.h"

#include <string>

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void set_session_id(long long /*session_id*/) {}
    virtual void append_analysis(const AnalysisResult& result) = 0;
    virtual void append_event_metadata(const PostureEvent& event, const ClipRef& clip_ref) = 0;
    virtual bool health_check() const { return true; }
    virtual void flush() = 0;
};
