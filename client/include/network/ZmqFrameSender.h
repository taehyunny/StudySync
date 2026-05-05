#pragma once

#include "network/IFrameSender.h"

#include <atomic>
#include <string>

class ZmqFrameSender final : public IFrameSender {
public:
    ZmqFrameSender(std::string endpoint, int jpeg_quality);

    bool initialize() override;
    bool send_frame(const Frame& frame) override;
    bool health_check() const override;
    void shutdown() override;

private:
    std::string endpoint_;
    int jpeg_quality_ = 80;
    std::atomic_bool initialized_{ false };
};
