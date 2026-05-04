#include "pch.h"
#include "network/HttpJsonlLogSink.h"

HttpJsonlLogSink::HttpJsonlLogSink(std::string endpoint, std::size_t flush_threshold)
    : endpoint_(std::move(endpoint))
    , uploader_(flush_threshold)
{
}

void HttpJsonlLogSink::append_analysis(const AnalysisResult& result)
{
    uploader_.append_analysis(result);
}

void HttpJsonlLogSink::append_event_metadata(const PostureEvent& event, const std::string& clip_ref)
{
    uploader_.append_event_metadata(event, clip_ref);
}

void HttpJsonlLogSink::flush()
{
    uploader_.flush_to_http(endpoint_);
}

