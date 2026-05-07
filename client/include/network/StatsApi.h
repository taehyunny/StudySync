#pragma once

#include "model/ServerStatsSnapshot.h"
#include "network/WinHttpClient.h"

#include <string>

class StatsApi {
public:
    explicit StatsApi(WinHttpClient& http);

    ServerStatsSnapshot::Data today();

private:
    static int extract_int_any(const std::string& json, const char* const* keys, int fallback = 0);
    static double extract_double_any(const std::string& json, const char* const* keys, double fallback = 0.0);
    static double extract_number(const std::string& json, const std::string& key, double fallback);
    static std::uint64_t now_ms();

    WinHttpClient& http_;
};
