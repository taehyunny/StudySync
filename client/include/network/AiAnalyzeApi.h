#pragma once

#include "model/AnalysisResult.h"
#include "model/Frame.h"
#include "network/WinHttpClient.h"

#include <optional>
#include <string>
#include <vector>

class AiAnalyzeApi {
public:
    AiAnalyzeApi(std::string base_url, int jpeg_quality);

    bool health_check() const;
    std::optional<AnalysisResult> analyze_frame(const Frame& frame);

private:
    static std::string base64_encode(const std::vector<unsigned char>& bytes);
    static std::string escape_json(const std::string& value);
    static std::string extract_json_string(const std::string& json, const std::string& key);
    static double extract_json_double(const std::string& json, const std::string& key, double fallback);
    static int extract_json_int(const std::string& json, const std::string& key, int fallback);
    static bool extract_json_bool(const std::string& json, const std::string& key, bool fallback);
    static std::optional<AnalysisResult> parse_response(const HttpResponse& response, std::uint64_t fallback_timestamp_ms);

    WinHttpClient http_;
    std::string base_url_;
    int jpeg_quality_ = 80;
};
