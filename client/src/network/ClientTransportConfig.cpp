#include "pch.h"
#include "network/ClientTransportConfig.h"

#include <utility>

ClientTransportConfig make_transport_config(
    std::string zmq_push_endpoint,
    std::string zmq_pull_endpoint,
    std::string jsonl_ingest_url,
    std::string clip_directory,
    int frame_sample_interval,
    int jpeg_quality,
    std::uint32_t local_clip_retention_days,
    std::size_t jsonl_flush_threshold)
{
    ClientTransportConfig config;
    config.frame_transport = FrameTransportKind::Zmq;
    config.log_transport = LogTransportKind::HttpJsonl;
    config.clip_store = ClipStoreKind::LocalClaimCheck;
    config.zmq_push_endpoint = std::move(zmq_push_endpoint);
    config.zmq_pull_endpoint = std::move(zmq_pull_endpoint);
    config.jsonl_ingest_url = std::move(jsonl_ingest_url);
    config.clip_directory = std::move(clip_directory);
    config.frame_sample_interval = frame_sample_interval;
    config.jpeg_quality = jpeg_quality;
    config.local_clip_retention_days = local_clip_retention_days;
    config.jsonl_flush_threshold = jsonl_flush_threshold;
    return config;
}
