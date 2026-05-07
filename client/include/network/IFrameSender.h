#pragma once

#include "model/Frame.h"

class IFrameSender {
public:
    virtual ~IFrameSender() = default;
    virtual bool initialize() = 0;
    virtual bool send_frame(const Frame& frame) = 0;
    virtual bool health_check() const { return true; }
    virtual void shutdown() = 0;
};
