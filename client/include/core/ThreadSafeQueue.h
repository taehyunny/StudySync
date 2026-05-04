#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class ThreadSafeQueue {
public:
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    bool try_pop(T& out)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) {
            return false;
        }

        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    T wait_pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};

