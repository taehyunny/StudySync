#pragma once

#include "alert/AlertQueue.h"

#include <atomic>
#include <thread>

class AlertDispatchThread {
public:
    explicit AlertDispatchThread(AlertQueue& alert_queue);
    ~AlertDispatchThread();

    void start();
    void stop();

private:
    void run();
    void show_popup(const Alert& alert);
    void send_to_arduino(const Alert& alert);

    AlertQueue& alert_queue_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};

