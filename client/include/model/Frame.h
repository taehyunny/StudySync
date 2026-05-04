#pragma once

#include "model/AnalysisResult.h"

#include <cstdint>
#include <opencv2/core/mat.hpp>

struct Frame {
    cv::Mat mat;
    AnalysisResult analysis;
    std::uint64_t timestamp_ms = 0;
    bool has_analysis = false;
};

