#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class FrameTransportKind {
    Zmq
};

enum class LogTransportKind {
    HttpJsonl
};

enum class ClipStoreKind {
    LocalClaimCheck
};

struct ClientTransportConfig {
    FrameTransportKind frame_transport = FrameTransportKind::Zmq;
    LogTransportKind log_transport = LogTransportKind::HttpJsonl;
    ClipStoreKind clip_store = ClipStoreKind::LocalClaimCheck;

    std::string zmq_push_endpoint = "tcp://127.0.0.1:5555";
    std::string zmq_pull_endpoint = "tcp://127.0.0.1:5556";
    std::string jsonl_ingest_url = "http://127.0.0.1:8000/logs/jsonl";
    std::string event_metadata_url = "http://127.0.0.1:8000/events";
    std::string clip_directory = "event_clips";

    int frame_sample_interval = 6;
    int jpeg_quality = 80;
    std::uint32_t local_clip_retention_days = 3;
    std::size_t jsonl_flush_threshold = 30;
};

ClientTransportConfig make_transport_config(
    std::string zmq_push_endpoint,
    std::string zmq_pull_endpoint,
    std::string jsonl_ingest_url,
    std::string clip_directory,
    int frame_sample_interval = 6,
    int jpeg_quality = 80,
    std::uint32_t local_clip_retention_days = 3,
    std::size_t jsonl_flush_threshold = 30);
