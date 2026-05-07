#pragma once

#include "model/AnalysisResult.h"
#include "model/PostureEvent.h"
#include "network/IEventClipStore.h"

#include <mutex>
#include <string>
#include <vector>

class JsonlBatchUploader {
public:
    explicit JsonlBatchUploader(std::size_t flush_threshold = 30);

    void append_analysis(const AnalysisResult& result);
    void append_event_metadata(const PostureEvent& event);
    void append_event_metadata(const PostureEvent& event, const ClipRef& clip_ref);
    std::string drain_jsonl();
    void flush_to_http(const std::string& endpoint);

private:
    static std::string escape_json(const std::string& value);
    static std::string to_jsonl(const AnalysisResult& result);
    static std::string to_jsonl(const PostureEvent& event);
    static std::string to_jsonl(const PostureEvent& event, const ClipRef& clip_ref);

    std::mutex mtx_;
    std::vector<std::string> lines_;
    std::size_t flush_threshold_ = 30;
};
