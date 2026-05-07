#pragma once

#include <cstdint>
#include <string>

enum class ClipStoreKind {
    LocalClaimCheck
};

struct ClientTransportConfig {
    ClipStoreKind clip_store = ClipStoreKind::LocalClaimCheck;

    // Main server uses HTTP for auth, sessions, logs, and claim-check metadata.
    std::string main_server_url = "http://10.10.10.100:8080";

    // AI server uses TCP for frame upload and analysis responses.
    std::string ai_server_host = "10.10.10.50";
    std::uint16_t ai_server_port = 9100;

    std::string clip_directory = "event_clips";

    int capture_fps = 30;
    int frame_sample_interval = 6; // 30fps capture / 6 = about 5fps AI sampling.
    int jpeg_quality = 80;
    std::uint32_t local_clip_retention_days = 3;
};
