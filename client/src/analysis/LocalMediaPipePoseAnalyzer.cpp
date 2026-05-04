#include "pch.h"
#include "analysis/LocalMediaPipePoseAnalyzer.h"

bool LocalMediaPipePoseAnalyzer::initialize()
{
    // TODO: initialize MediaPipe Tasks Pose Landmarker when the C++ runtime
    // dependency strategy is finalized.
    return true;
}

std::optional<AnalysisResult> LocalMediaPipePoseAnalyzer::analyze(const Frame& frame)
{
    (void)frame;
    // TODO: convert cv::Mat BGR -> RGB input image, run pose landmarker,
    // then map the 33 landmarks into AnalysisResult.
    return std::nullopt;
}

void LocalMediaPipePoseAnalyzer::shutdown()
{
}

