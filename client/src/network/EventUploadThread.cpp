#include "pch.h"
#include "network/EventUploadThread.h"

#include <chrono>

EventUploadThread::EventUploadThread(EventQueue& event_queue, IEventClipStore& clip_store, ILogSink& log_sink)
    : event_queue_(event_queue)
    , clip_store_(clip_store)
    , log_sink_(log_sink)
{
}

EventUploadThread::~EventUploadThread()
{
    stop();
}

void EventUploadThread::start()
{
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&EventUploadThread::run, this);
}

void EventUploadThread::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void EventUploadThread::run()
{
    while (running_) {
        PostureEvent event;
        if (!event_queue_.try_pop(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        upload_event(event);
    }
}

void EventUploadThread::upload_event(const PostureEvent& event)
{
    const ClipRef clip_ref = clip_store_.store_clip(event);
    log_sink_.append_event_metadata(event, clip_ref);
    log_sink_.flush();
}
