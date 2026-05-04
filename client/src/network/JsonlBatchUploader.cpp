#include "pch.h"
#include "network/JsonlBatchUploader.h"

#include <sstream>

JsonlBatchUploader::JsonlBatchUploader(std::size_t flush_threshold)
    : flush_threshold_(flush_threshold)
{
}

void JsonlBatchUploader::append_analysis(const AnalysisResult& result)
{
    std::lock_guard<std::mutex> lock(mtx_);
    lines_.push_back(to_jsonl(result));
}

void JsonlBatchUploader::append_event_metadata(const PostureEvent& event)
{
    std::lock_guard<std::mutex> lock(mtx_);
    lines_.push_back(to_jsonl(event));
}

void JsonlBatchUploader::append_event_metadata(const PostureEvent& event, const std::string& clip_ref)
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::string line = to_jsonl(event);
    if (!clip_ref.empty() && line.size() >= 1 && line.back() == '}') {
        line.pop_back();
        line += ",\"clip_ref\":\"" + escape_json(clip_ref) + "\"}";
    }
    lines_.push_back(std::move(line));
}

std::string JsonlBatchUploader::drain_jsonl()
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::ostringstream out;
    for (const auto& line : lines_) {
        out << line << '\n';
    }
    lines_.clear();
    return out.str();
}

void JsonlBatchUploader::flush_to_http(const std::string& endpoint)
{
    (void)endpoint;
    const std::string body = drain_jsonl();
    if (body.empty()) {
        return;
    }

    // TODO: POST body as application/x-ndjson with WinHTTP.
}

std::string JsonlBatchUploader::escape_json(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string JsonlBatchUploader::to_jsonl(const AnalysisResult& result)
{
    std::ostringstream out;
    out << "{\"kind\":\"analysis\""
        << ",\"timestamp_ms\":" << result.timestamp_ms
        << ",\"focus_score\":" << result.focus_score
        << ",\"state\":\"" << escape_json(result.state) << "\""
        << ",\"neck_angle\":" << result.neck_angle
        << ",\"shoulder_diff\":" << result.shoulder_diff
        << ",\"ear\":" << result.ear
        << ",\"posture_ok\":" << (result.posture_ok ? "true" : "false")
        << ",\"drowsy\":" << (result.drowsy ? "true" : "false")
        << ",\"absent\":" << (result.absent ? "true" : "false")
        << "}";
    return out.str();
}

std::string JsonlBatchUploader::to_jsonl(const PostureEvent& event)
{
    std::ostringstream out;
    out << "{\"kind\":\"event\""
        << ",\"timestamp_ms\":" << event.timestamp_ms
        << ",\"reason\":\"" << escape_json(event.reason) << "\""
        << ",\"frame_count\":" << event.frames.size()
        << "}";
    return out.str();
}
