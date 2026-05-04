#include "pch.h"
#include "network/ZmqRecvThread.h"

#include <chrono>

ZmqRecvThread::ZmqRecvThread(EventShadowBuffer& shadow_buffer, EventQueue& event_queue)
    : shadow_buffer_(shadow_buffer)
    , event_queue_(event_queue)
{
    detector_.set_callback([this](PostureEvent event) {
        event_queue_.push(std::move(event));
    });
}

ZmqRecvThread::~ZmqRecvThread()
{
    stop();
}

void ZmqRecvThread::start(const std::string& endpoint)
{
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&ZmqRecvThread::run, this, endpoint);
}

void ZmqRecvThread::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ZmqRecvThread::run(std::string endpoint)
{
    (void)endpoint;

    while (running_) {
        // TODO: receive JSON from ZMQ PULL and parse into AnalysisResult.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

