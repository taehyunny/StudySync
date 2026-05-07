#include "pch.h"
#include "network/AiAnalyzeThread.h"

#include <chrono>
#include <sstream>
#include <windows.h>

namespace {
void log_ai_thread(const char* message)
{
    std::ostringstream out;
    out << "[StudySync][AI] " << message << "\n";
    OutputDebugStringA(out.str().c_str());
}
}

AiAnalyzeThread::AiAnalyzeThread(
    CaptureThread::SendFrameBuffer& frame_buffer,
    AiAnalyzeApi& ai_api,
    EventShadowBuffer& shadow_buffer,
    EventQueue& event_queue,
    AnalysisResultBuffer& result_buffer)
    : frame_buffer_(frame_buffer)
    , ai_api_(ai_api)
    , shadow_buffer_(shadow_buffer)
    , event_queue_(event_queue)
    , result_buffer_(result_buffer)
{
    detector_.set_callback([this](PostureEvent event) {
        event_queue_.push(std::move(event));
    });
}

AiAnalyzeThread::~AiAnalyzeThread()
{
    stop();
}

void AiAnalyzeThread::start(int sample_interval)
{
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&AiAnalyzeThread::run, this, sample_interval);
}

void AiAnalyzeThread::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiAnalyzeThread::run(int sample_interval)
{
    int frame_index = 0;
    if (sample_interval <= 0) {
        sample_interval = 1;
    }

    log_ai_thread("analyze thread started");

    while (running_) {
        Frame frame;
        if (!frame_buffer_.try_pop(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        int consumed = 1;
        Frame newer;
        while (frame_buffer_.try_pop(newer)) {
            frame = std::move(newer);
            ++consumed;
        }

        frame_index += consumed;
        if (frame_index < sample_interval) {
            continue;
        }
        frame_index = 0;

        const auto result = ai_api_.analyze_frame(frame);
        if (!result.has_value()) {
            continue;
        }

        result_buffer_.update(*result);
        detector_.feed(*result, shadow_buffer_);
    }

    log_ai_thread("analyze thread stopped");
}
