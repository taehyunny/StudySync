#pragma once

#include "capture/CaptureThread.h"
#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "event/PostureEventDetector.h"
#include "model/AnalysisResult.h"
#include "model/AnalysisResultBuffer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>

// Owns the client-to-AI TCP connection.
// Responsibility:
// - sample the latest send buffer frame
// - encode it as JPEG
// - send a length-prefixed JSON header + JPEG payload
// - receive analysis JSON on the same socket
// - update renderer/event detector state
class AiTcpClient {
public:
    AiTcpClient(CaptureThread::SendFrameBuffer& send_buffer,
                EventShadowBuffer& shadow_buffer,
                EventQueue& event_queue,
                AnalysisResultBuffer& result_buffer,
                int jpeg_quality);
    ~AiTcpClient();

    void start(const std::string& host, std::uint16_t port, long long session_id, int sample_interval);
    void stop();

    bool is_connected() const { return connected_.load(); }

private:
    void run(std::string host, std::uint16_t port, long long session_id, int sample_interval);

    SOCKET connect_to(const std::string& host, std::uint16_t port);
    void close_socket(SOCKET& socket);

    bool send_frame_packet(SOCKET socket, const Frame& frame, long long session_id);
    bool recv_result_packet(SOCKET socket, AnalysisResult& out);

    static bool send_all(SOCKET socket, const char* data, int length);
    static bool recv_all(SOCKET socket, char* data, int length);
    static bool send_json_with_binary(SOCKET socket,
                                      const std::string& json,
                                      const std::vector<unsigned char>& binary);

    static std::string now_iso8601();
    static std::string extract_string(const std::string& json, const std::string& key);
    static double extract_number(const std::string& json, const std::string& key, double fallback = 0.0);
    static bool extract_bool(const std::string& json, const std::string& key, bool fallback = false);

    CaptureThread::SendFrameBuffer& send_buffer_;
    EventShadowBuffer& shadow_buffer_;
    EventQueue& event_queue_;
    AnalysisResultBuffer& result_buffer_;
    PostureEventDetector detector_;

    int jpeg_quality_ = 80;
    std::atomic_bool running_{ false };
    std::atomic_bool connected_{ false };
    std::thread worker_;
};
