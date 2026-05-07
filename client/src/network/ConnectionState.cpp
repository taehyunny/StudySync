#include "pch.h"
#include "network/ConnectionState.h"

const char* to_string(ConnectionStatus status)
{
    switch (status) {
    case ConnectionStatus::Disconnected: return "Disconnected";
    case ConnectionStatus::Connecting: return "Connecting";
    case ConnectionStatus::Connected: return "Connected";
    case ConnectionStatus::Warning: return "Warning";
    case ConnectionStatus::Reconnecting: return "Reconnecting";
    default: return "Unknown";
    }
}

