#include "pch.h"
#include "network/AiAnalyzeApi.h"

#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <utility>
#include <vector>

AiAnalyzeApi::AiAnalyzeApi(std::string base_url, int jpeg_quality)
    : base_url_(std::move(base_url))
    , jpeg_quality_(jpeg_quality)
{
    http_.set_base_url(base_url_);
}

bool AiAnalyzeApi::health_check() const
{
    return !base_url_.empty();
}

std::optional<AnalysisResult> AiAnalyzeApi::analyze_frame(const Frame& frame)
{
    if (frame.mat.empty()) {
        return std::nullopt;
    }

    std::vector<unsigned char> jpeg;
    const std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, jpeg_quality_ };
    if (!cv::imencode(".jpg", frame.mat, jpeg, params)) {
        return std::nullopt;
    }

    std::ostringstream body;
    body << "{\"timestamp_ms\":" << frame.timestamp_ms
         << ",\"format\":\"jpeg\""
         << ",\"frame_base64\":\"" << base64_encode(jpeg) << "\"}";

    const HttpResponse response = http_.post_json("/analyze/frame", body.str());
    return parse_response(response, frame.timestamp_ms);
}

std::string AiAnalyzeApi::base64_encode(const std::vector<unsigned char>& bytes)
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const unsigned int b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;

        encoded += table[(b0 >> 2) & 0x3F];
        encoded += table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        encoded += (i + 1 < bytes.size()) ? table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
        encoded += (i + 2 < bytes.size()) ? table[b2 & 0x3F] : '=';
    }

    return encoded;
}

std::string AiAnalyzeApi::escape_json(const std::string& value)
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

std::string AiAnalyzeApi::extract_json_string(const std::string& json, const std::string& key)
{
    const std::string pattern = "\"" + key + "\":\"";
    const auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }

    const auto start = pos + pattern.size();
    std::string result;
    for (auto i = start; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            result += json[++i];
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i];
        }
    }
    return result;
}

double AiAnalyzeApi::extract_json_double(const std::string& json, const std::string& key, double fallback)
{
    const std::string pattern = "\"" + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        return fallback;
    }

    pos += pattern.size();
    while (pos < json.size() && json[pos] == ' ') {
        ++pos;
    }

    std::string value;
    for (auto i = pos; i < json.size(); ++i) {
        const char ch = json[i];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+'
            || ch == '.' || ch == 'e' || ch == 'E') {
            value += ch;
        } else {
            break;
        }
    }

    if (value.empty()) {
        return fallback;
    }
    return std::stod(value);
}

int AiAnalyzeApi::extract_json_int(const std::string& json, const std::string& key, int fallback)
{
    return static_cast<int>(extract_json_double(json, key, fallback));
}

bool AiAnalyzeApi::extract_json_bool(const std::string& json, const std::string& key, bool fallback)
{
    const std::string pattern = "\"" + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        return fallback;
    }

    pos += pattern.size();
    while (pos < json.size() && json[pos] == ' ') {
        ++pos;
    }

    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

std::optional<AnalysisResult> AiAnalyzeApi::parse_response(const HttpResponse& response, std::uint64_t fallback_timestamp_ms)
{
    if (!response.ok()) {
        return std::nullopt;
    }

    AnalysisResult result;
    result.timestamp_ms = static_cast<std::uint64_t>(
        extract_json_double(response.body, "timestamp_ms", static_cast<double>(fallback_timestamp_ms)));
    result.focus_score = extract_json_int(response.body, "focus_score", 0);
    result.state = extract_json_string(response.body, "state");
    result.guide = extract_json_string(response.body, "guide");
    result.neck_angle = extract_json_double(response.body, "neck_angle", 0.0);
    result.shoulder_diff = extract_json_double(response.body, "shoulder_diff", 0.0);
    result.ear = extract_json_double(response.body, "ear", 1.0);
    result.posture_ok = extract_json_bool(response.body, "posture_ok", true);
    result.drowsy = extract_json_bool(response.body, "drowsy", false);
    result.absent = extract_json_bool(response.body, "absent", false);

    return result;
}
