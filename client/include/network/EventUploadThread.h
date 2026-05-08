#pragma once

#include "event/EventQueue.h"
#include "model/ReviewEventStore.h"
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

    // 복기 화면용 이벤트 저장소 연결 (선택, nullptr이면 비활성)
    void set_review_store(ReviewEventStore* store) { review_store_ = store; }

private:
    void run();
    void upload_event(const PostureEvent& event);

    EventQueue&       event_queue_;
    IEventClipStore&  clip_store_;
    ILogSink&         log_sink_;
    ReviewEventStore* review_store_ = nullptr;  // 복기 화면용 (optional)

    std::atomic_bool running_{ false };
    std::thread      worker_;
};
