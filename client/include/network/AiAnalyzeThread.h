#pragma once

#include "capture/CaptureThread.h"
#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "event/PostureEventDetector.h"
#include "model/AnalysisResultBuffer.h"
#include "network/AiAnalyzeApi.h"

#include <atomic>
#include <thread>

class AiAnalyzeThread {
public:
    AiAnalyzeThread(
        CaptureThread::SendFrameBuffer& frame_buffer,
        AiAnalyzeApi& ai_api,
        EventShadowBuffer& shadow_buffer,
        EventQueue& event_queue,
        AnalysisResultBuffer& result_buffer);
    ~AiAnalyzeThread();

    void start(int sample_interval = 6);
    void stop();

private:
    void run(int sample_interval);

    CaptureThread::SendFrameBuffer& frame_buffer_;
    AiAnalyzeApi& ai_api_;
    EventShadowBuffer& shadow_buffer_;
    EventQueue& event_queue_;
    AnalysisResultBuffer& result_buffer_;
    PostureEventDetector detector_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};
