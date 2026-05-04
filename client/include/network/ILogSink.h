#pragma once

#include "model/AnalysisResult.h"
#include "model/PostureEvent.h"

#include <string>

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void append_analysis(const AnalysisResult& result) = 0;
    virtual void append_event_metadata(const PostureEvent& event, const std::string& clip_ref) = 0;
    virtual void flush() = 0;
};
