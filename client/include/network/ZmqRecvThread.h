#pragma once

#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "event/PostureEventDetector.h"

#include <atomic>
#include <string>
#include <thread>

class ZmqRecvThread {
public:
    ZmqRecvThread(EventShadowBuffer& shadow_buffer, EventQueue& event_queue);
    ~ZmqRecvThread();

    void start(const std::string& endpoint);
    void stop();

private:
    void run(std::string endpoint);

    EventShadowBuffer& shadow_buffer_;
    EventQueue& event_queue_;
    PostureEventDetector detector_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};

