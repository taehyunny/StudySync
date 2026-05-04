#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Landmark2D {
    float x = 0.0f;
    float y = 0.0f;
    float visibility = 0.0f;
};

struct AnalysisResult {
    std::uint64_t timestamp_ms = 0;
    double neck_angle = 0.0;
    double shoulder_diff = 0.0;
    double ear = 1.0;
    int focus_score = 0;
    bool posture_ok = true;
    bool drowsy = false;
    bool absent = false;
    std::string state;
    std::string guide;
    std::vector<Landmark2D> landmarks;
};

