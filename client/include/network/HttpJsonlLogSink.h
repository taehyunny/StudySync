#pragma once

#include "network/ILogSink.h"
#include "network/JsonlBatchUploader.h"

#include <string>

class HttpJsonlLogSink final : public ILogSink {
public:
    HttpJsonlLogSink(std::string endpoint, std::size_t flush_threshold);

    void append_analysis(const AnalysisResult& result) override;
    void append_event_metadata(const PostureEvent& event, const ClipRef& clip_ref) override;
    void flush() override;

private:
    std::string endpoint_;
    JsonlBatchUploader uploader_;
};
