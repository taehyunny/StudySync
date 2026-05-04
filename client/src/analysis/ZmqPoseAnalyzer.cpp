#include "pch.h"
#include "analysis/ZmqPoseAnalyzer.h"

ZmqPoseAnalyzer::ZmqPoseAnalyzer(std::string push_endpoint, std::string pull_endpoint)
    : push_endpoint_(std::move(push_endpoint))
    , pull_endpoint_(std::move(pull_endpoint))
{
}

bool ZmqPoseAnalyzer::initialize()
{
    // TODO: initialize ZMQ PUSH/PULL sockets.
    return true;
}

std::optional<AnalysisResult> ZmqPoseAnalyzer::analyze(const Frame& frame)
{
    (void)frame;
    // TODO: encode one sampled frame, send it to pose_server.py, and return
    // the latest received result if available.
    return std::nullopt;
}

void ZmqPoseAnalyzer::shutdown()
{
}

