#pragma once

#include "model/AnalysisResult.h"
#include <mutex>

// 스레드 안전 최신 분석결과 버퍼 — AiTcpClient(쓰기) / RenderThread(읽기)
class AnalysisResultBuffer {
public:
    void update(const AnalysisResult& result) {
        std::lock_guard<std::mutex> lock(mtx_);
        result_   = result;
        has_data_ = true;
    }

    AnalysisResult read() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return result_;
    }

    bool has_data() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return has_data_;
    }

private:
    mutable std::mutex mtx_;
    AnalysisResult     result_;
    bool               has_data_ = false;
};
