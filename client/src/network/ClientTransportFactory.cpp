#include "pch.h"
#include "network/ClientTransportFactory.h"

#include "network/HttpJsonlLogSink.h"
#include "network/LocalClaimCheckClipStore.h"

ClientTransports make_client_transports(const ClientTransportConfig& config)
{
    ClientTransports transports;

    // endpoint는 WinHttpClient::post_ndjson 에 path로 전달되므로
    // 절대 URL이 아닌 path만 사용해야 한다. host/port는 WinHttpClient가
    // set_base_url()로 관리한다.
    transports.log_sink = std::make_unique<HttpJsonlLogSink>("/log/ingest", 30);

    transports.clip_store = std::make_unique<LocalClaimCheckClipStore>(
        config.clip_directory,
        config.local_clip_retention_days);

    return transports;
}
