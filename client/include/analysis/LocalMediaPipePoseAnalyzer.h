#pragma once

#include "analysis/IPoseAnalyzer.h"

class LocalMediaPipePoseAnalyzer final : public IPoseAnalyzer {
public:
    bool initialize() override;
    std::optional<AnalysisResult> analyze(const Frame& frame) override;
    void shutdown() override;
};

