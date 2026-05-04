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
    return true;
}

bool ZmqFrameSender::send_frame(const Frame& frame)
{
    std::vector<unsigned char> jpeg;
    const std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, jpeg_quality_ };
    if (!cv::imencode(".jpg", frame.mat, jpeg, params)) {
        return false;
    }

    // TODO: send timestamp + jpeg payload through ZMQ.
    return true;
}

void ZmqFrameSender::shutdown()
{
    // TODO: close ZMQ socket.
}
