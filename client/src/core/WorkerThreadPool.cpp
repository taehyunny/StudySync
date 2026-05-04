#include "pch.h"
#include "core/WorkerThreadPool.h"

WorkerThreadPool::WorkerThreadPool(std::size_t worker_count)
    : worker_count_(worker_count)
{
}

WorkerThreadPool::~WorkerThreadPool()
{
    stop();
}

void WorkerThreadPool::start()
{
    if (running_.exchange(true)) {
        return;
    }

    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back(&WorkerThreadPool::worker_loop, this);
    }
}

void WorkerThreadPool::stop()
{
    running_ = false;
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void WorkerThreadPool::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void WorkerThreadPool::worker_loop()
{
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });

            if (!running_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        if (task) {
            task();
        }
    }
}

