#pragma once

#include "analysis/IPoseAnalyzer.h"

#include <string>

class ZmqPoseAnalyzer final : public IPoseAnalyzer {
public:
    ZmqPoseAnalyzer(std::string push_endpoint, std::string pull_endpoint);

    bool initialize() override;
    std::optional<AnalysisResult> analyze(const Frame& frame) override;
    void shutdown() override;

private:
    std::string push_endpoint_;
    std::string pull_endpoint_;
};

