#include "pch.h"
#include "network/LocalClaimCheckClipStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <vector>

namespace {
std::string escape_json(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

std::uint64_t now_ms()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
}

LocalClaimCheckClipStore::LocalClaimCheckClipStore(std::string clip_directory, std::uint32_t retention_days)
    : clip_directory_(std::move(clip_directory))
    , retention_days_(retention_days)
{
}

ClipRef LocalClaimCheckClipStore::store_clip(const PostureEvent& event)
{
    namespace fs = std::filesystem;

    const std::string clip_id = "local:event_" + std::to_string(event.timestamp_ms);
    const std::uint64_t created_at = now_ms();
    const std::uint64_t expires_at = created_at + static_cast<std::uint64_t>(retention_days_) * 24ULL * 60ULL * 60ULL * 1000ULL;
    fs::path event_dir = fs::path(clip_directory_) / ("event_" + std::to_string(event.timestamp_ms));
    fs::create_directories(event_dir);

    // Store a JPEG sequence first. This keeps the claim-check path useful even
    // before the MP4 encoder policy is finalized.
    std::size_t written = 0;
    for (std::size_t i = 0; i < event.frames.size(); ++i) {
        if (event.frames[i].mat.empty()) {
            continue;
        }

        std::ostringstream name;
        name << "frame_" << std::setw(4) << std::setfill('0') << i << ".jpg";
        const fs::path frame_path = event_dir / name.str();
        const std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
        if (cv::imwrite(frame_path.string(), event.frames[i].mat, params)) {
            ++written;
        }
    }

    const fs::path manifest_path = event_dir / "manifest.jsonl";
    std::ofstream manifest(manifest_path, std::ios::binary | std::ios::trunc);
    manifest << "{\"kind\":\"event_clip\""
             << ",\"clip_id\":\"" << escape_json(clip_id) << "\""
             << ",\"clip_access\":\"local_only\""
             << ",\"clip_format\":\"jpeg_sequence\""
             << ",\"timestamp_ms\":" << event.timestamp_ms
             << ",\"reason\":\"" << escape_json(event.reason) << "\""
             << ",\"confidence\":" << event.confidence
             << ",\"frame_count\":" << written
             << ",\"retention_days\":" << retention_days_
             << ",\"created_at_ms\":" << created_at
             << ",\"expires_at_ms\":" << expires_at
             << "}\n";

    ClipRef ref;
    ref.clip_id = clip_id;
    ref.uri = event_dir.string();
    ref.access_kind = "local_only";
    ref.format = "jpeg_sequence";
    ref.frame_count = written;
    ref.retention_days = retention_days_;
    ref.created_at_ms = created_at;
    ref.expires_at_ms = expires_at;
    return ref;
}
