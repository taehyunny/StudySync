#include "pch.h"
#include "network/ZmqFrameSender.h"

#include <opencv2/imgcodecs.hpp>
#include <vector>

ZmqFrameSender::ZmqFrameSender(std::string endpoint, int jpeg_quality)
    : endpoint_(std::move(endpoint))
    , jpeg_quality_(jpeg_quality)
{
}

bool ZmqFrameSender::initialize()
{
    // TODO: open ZMQ PUSH socket with endpoint_.
    initialized_ = true;
    return true;
}

bool ZmqFrameSender::send_frame(const Frame& frame)
{
    if (!initialized_) {
        return false;
    }

    std::vector<unsigned char> jpeg;
    const std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, jpeg_quality_ };
    if (!cv::imencode(".jpg", frame.mat, jpeg, params)) {
        return false;
    }

    // TODO: send timestamp + jpeg payload through ZMQ.
    return true;
}

bool ZmqFrameSender::health_check() const
{
    return initialized_;
}

void ZmqFrameSender::shutdown()
{
    // TODO: close ZMQ socket.
    initialized_ = false;
}
