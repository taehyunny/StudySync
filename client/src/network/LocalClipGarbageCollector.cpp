#include "pch.h"
#include "network/LocalClipGarbageCollector.h"

#include <filesystem>
#include <sstream>
#include <windows.h>

LocalClipGarbageCollector::LocalClipGarbageCollector(
    std::string clip_directory,
    std::uint32_t retention_days,
    std::chrono::minutes scan_interval)
    : clip_directory_(std::move(clip_directory))
    , retention_days_(retention_days)
    , scan_interval_(scan_interval)
{
}

LocalClipGarbageCollector::~LocalClipGarbageCollector()
{
    stop();
}

void LocalClipGarbageCollector::start()
{
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&LocalClipGarbageCollector::run_loop, this);
}

void LocalClipGarbageCollector::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LocalClipGarbageCollector::run_once()
{
    namespace fs = std::filesystem;

    const fs::path root(clip_directory_);
    if (!fs::exists(root)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }

        const fs::path path = entry.path();
        if (!is_expired(path)) {
            continue;
        }

        std::error_code ec;
        fs::remove_all(path, ec);
        if (!ec) {
            log_delete(path.string());
        }
    }
}

bool LocalClipGarbageCollector::is_expired(const std::filesystem::path& path) const
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto last_write = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }

    const auto now = fs::file_time_type::clock::now();
    const auto retention = std::chrono::hours(24 * retention_days_);
    return now - last_write > retention;
}

void LocalClipGarbageCollector::run_loop()
{
    while (running_) {
        run_once();
        std::this_thread::sleep_for(scan_interval_);
    }
}

void LocalClipGarbageCollector::log_delete(const std::string& path) const
{
    std::ostringstream out;
    out << "[StudySync][ClipGC] deleted expired local clip path=" << path << "\n";
    OutputDebugStringA(out.str().c_str());
}

