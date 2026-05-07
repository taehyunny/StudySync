#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class ClipStoreKind {
    LocalClaimCheck
};

struct ClientTransportConfig {
    ClipStoreKind clip_store = ClipStoreKind::LocalClaimCheck;

    // 메인서버 HTTP (인증/세션/통계)
    std::string main_server_url = "http://10.10.10.100:8080";

    // AI서버 TCP (프레임 전송 + 분석 결과 수신)
    std::string ai_server_url = "http://10.10.10.50:9100";
    std::string ai_server_host = "10.10.10.50";
    uint16_t    ai_server_port = 9100;

    std::string clip_directory = "event_clips";

    int capture_fps            = 30;
    int frame_sample_interval  = 6;   // N 프레임마다 1장 전송 → 30fps 기준 5fps
    int jpeg_quality           = 80;
    std::uint32_t local_clip_retention_days = 3;
};
