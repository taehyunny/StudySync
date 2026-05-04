#pragma once

#include "event/EventQueue.h"
#include "network/IEventClipStore.h"
#include "network/ILogSink.h"

#include <atomic>
#include <thread>

class EventUploadThread {
public:
    EventUploadThread(EventQueue& event_queue, IEventClipStore& clip_store, ILogSink& log_sink);
    ~EventUploadThread();

    void start();
    void stop();

private:
    void run();
    void upload_event(const PostureEvent& event);

    EventQueue& event_queue_;
    IEventClipStore& clip_store_;
    ILogSink& log_sink_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};
