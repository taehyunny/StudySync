#include "pch.h"
#include "network/ClientTransportFactory.h"

#include "network/HttpJsonlLogSink.h"
#include "network/LocalClaimCheckClipStore.h"
#include "network/ZmqFrameSender.h"

ClientTransports make_client_transports(const ClientTransportConfig& config)
{
    ClientTransports transports;

    switch (config.frame_transport) {
    case FrameTransportKind::Zmq:
        transports.frame_sender = std::make_unique<ZmqFrameSender>(config.zmq_push_endpoint, config.jpeg_quality);
        break;
    }

    switch (config.log_transport) {
    case LogTransportKind::HttpJsonl:
        transports.log_sink = std::make_unique<HttpJsonlLogSink>(config.jsonl_ingest_url, config.jsonl_flush_threshold);
        break;
    }

    switch (config.clip_store) {
    case ClipStoreKind::LocalClaimCheck:
        transports.clip_store = std::make_unique<LocalClaimCheckClipStore>(config.clip_directory);
        break;
    }

    return transports;
}

