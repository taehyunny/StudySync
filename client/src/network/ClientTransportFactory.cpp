#include "pch.h"
#include "network/ClientTransportFactory.h"

#include "network/HttpJsonlLogSink.h"
#include "network/LocalClaimCheckClipStore.h"

ClientTransports make_client_transports(const ClientTransportConfig& config)
{
    ClientTransports transports;

    transports.log_sink = std::make_unique<HttpJsonlLogSink>(
        config.main_server_url + "/log/ingest", 30);

    transports.clip_store = std::make_unique<LocalClaimCheckClipStore>(
        config.clip_directory,
        config.local_clip_retention_days);

    return transports;
}
