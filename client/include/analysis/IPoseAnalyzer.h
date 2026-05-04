#pragma once

#include "model/AnalysisResult.h"
#include "model/Frame.h"

#include <optional>

class IPoseAnalyzer {
public:
    virtual ~IPoseAnalyzer() = default;

    virtual bool initialize() = 0;
    virtual std::optional<AnalysisResult> analyze(const Frame& frame) = 0;
    virtual void shutdown() = 0;
};

