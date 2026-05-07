#pragma once

#include "network/ClientTransportConfig.h"
#include "network/IEventClipStore.h"
#include "network/ILogSink.h"

#include <memory>

struct ClientTransports {
    std::unique_ptr<ILogSink>        log_sink;
    std::unique_ptr<IEventClipStore> clip_store;
};

ClientTransports make_client_transports(const ClientTransportConfig& config);

