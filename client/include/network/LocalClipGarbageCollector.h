#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

class LocalClipGarbageCollector {
public:
    LocalClipGarbageCollector(
        std::string clip_directory,
        std::uint32_t retention_days = 3,
        std::chrono::minutes scan_interval = std::chrono::minutes(30));
    ~LocalClipGarbageCollector();

    void start();
    void stop();
    void run_once();

private:
    bool is_expired(const std::filesystem::path& path) const;
    void run_loop();
    void log_delete(const std::string& path) const;

    std::string clip_directory_;
    std::uint32_t retention_days_ = 3;
    std::chrono::minutes scan_interval_;
    std::atomic_bool running_{ false };
    std::thread worker_;
};
