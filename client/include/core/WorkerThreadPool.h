#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class WorkerThreadPool {
public:
    explicit WorkerThreadPool(std::size_t worker_count = 3);
    ~WorkerThreadPool();

    void start();
    void stop();
    void enqueue(std::function<void()> task);

private:
    void worker_loop();

    std::size_t worker_count_ = 3;
    std::atomic_bool running_{ false };
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
};

