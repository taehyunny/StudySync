#include "pch.h"
#include "alert/AlertDispatchThread.h"

#include <chrono>

AlertDispatchThread::AlertDispatchThread(AlertQueue& alert_queue)
    : alert_queue_(alert_queue)
{
}

AlertDispatchThread::~AlertDispatchThread()
{
    stop();
}

void AlertDispatchThread::start()
{
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&AlertDispatchThread::run, this);
}

void AlertDispatchThread::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AlertDispatchThread::run()
{
    while (running_) {
        Alert alert;
        if (!alert_queue_.try_pop(alert)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (alert.target == AlertTarget::Popup || alert.target == AlertTarget::Both) {
            show_popup(alert);
        }

        if (alert.target == AlertTarget::Arduino || alert.target == AlertTarget::Both) {
            send_to_arduino(alert);
        }
    }
}

void AlertDispatchThread::show_popup(const Alert& alert)
{
    (void)alert;
    // TODO: marshal to UI thread and display an MFC popup/toast.
}

void AlertDispatchThread::send_to_arduino(const Alert& alert)
{
    (void)alert;
    // TODO: send a compact command over serial, e.g. posture_warn or stretch.
}

