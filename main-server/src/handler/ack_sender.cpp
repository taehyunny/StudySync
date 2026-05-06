// ============================================================================
// ack_sender.cpp — ACK 응답 송신
// ============================================================================
// StudySync 흐름:
//   FocusService / PostureService / TrainHandler
//     ─AckSendEvent─> [AckSender] ─TCP send─> AI 측
//
// JSON 본문 키는 "request_id" 통일 (옛 "inspection_id" 폐기).
// ============================================================================
#include "handler/ack_sender.h"
#include "monitor/connection_registry.h"
#include "Protocol.h"

#include "core/logger.h"
#include "core/tcp_utils.h"
#include "security/json_safety.h"

#include <chrono>
#include <sstream>
#include <thread>

using factory::security::escape_json;

namespace factory {

AckSender::AckSender(EventBus& bus)
    : event_bus_(bus) {
}

void AckSender::register_handlers() {
    event_bus_.subscribe(EventType::ACK_SEND_REQUESTED,
                         [this](const std::any& p) { this->on_ack_send_requested(p); });
    event_bus_.subscribe(EventType::MODEL_RELOAD_REQUESTED,
                         [this](const std::any& p) { this->on_model_reload_requested(p); });
}

void AckSender::on_ack_send_requested(const std::any& payload) {
    const auto& ev = std::any_cast<const AckSendEvent&>(payload);
    send_ack(ev.sender_addr, ev.protocol_no, ev.request_id,
             ev.ack_ok, ev.error_message);
}

bool AckSender::send_ack(const std::string& sender_addr,
                         int protocol_no,
                         const std::string& request_id,
                         bool ack_ok,
                         const std::string& error_message) {
    int fd = ConnectionRegistry::instance().find_fd(sender_addr);
    if (fd < 0) {
        log_err_ack("연결 없음 | addr=%s", sender_addr.c_str());
        return false;
    }

    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":" << protocol_no << ","
       << "\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\","
       << "\"request_id\":\"" << escape_json(request_id) << "\","
       << "\"ack\":" << (ack_ok ? "true" : "false");
    if (!ack_ok) {
        os << ",\"error_message\":\"" << escape_json(error_message) << "\"";
    }
    os << ",\"image_size\":0"
       << "}";
    std::string json_body = os.str();

    if (!send_json_frame(fd, json_body)) {
        log_err_ack("전송 실패 | fd=%d addr=%s", fd, sender_addr.c_str());
        return false;
    }
    log_ack("ACK 전송 | no=%d req=%s → %s",
            protocol_no, request_id.c_str(), sender_addr.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// MODEL_RELOAD_REQUESTED — 학습 완료 후 추론서버 새 모델 송신
// 지수 백오프 재시도 (1→5→30→120→300초). 영구 장애 시 5회 후 포기.
// StudySync: station_id 분기 없음 — 단일 추론서버에 model_type 만 전달.
// ---------------------------------------------------------------------------
void AckSender::on_model_reload_requested(const std::any& payload) {
    const auto& ev = std::any_cast<const ModelReloadEvent&>(payload);

    log_train("MODEL_RELOAD_CMD 전송 시작 | type=%s 버전=%s 크기=%zu bytes",
              ev.model_type.c_str(), ev.version.c_str(), ev.model_bytes.size());

    constexpr int delays[]   = {1, 5, 30, 120, 300};
    constexpr int MAX_RETRY  = sizeof(delays) / sizeof(delays[0]);

    for (int attempt = 0; attempt < MAX_RETRY; ++attempt) {
        if (send_model_reload(ev.model_type, ev.model_path, ev.version, ev.model_bytes)) {
            if (attempt > 0) {
                log_train("MODEL_RELOAD_CMD 재시도 성공 | attempt=%d/%d",
                          attempt + 1, MAX_RETRY);
            }
            return;
        }
        if (attempt < MAX_RETRY - 1) {
            log_retry("TRAIN", "MODEL_RELOAD_CMD 재시도 | %d/%d (다음 %d초 대기)",
                      attempt + 1, MAX_RETRY, delays[attempt]);
            std::this_thread::sleep_for(std::chrono::seconds(delays[attempt]));
        }
    }
    log_err_train("MODEL_RELOAD_CMD 최종 실패 | type=%s (5회 시도)", ev.model_type.c_str());
}

bool AckSender::send_model_reload(const std::string& model_type,
                                   const std::string& model_path,
                                   const std::string& version,
                                   const std::vector<uint8_t>& model_bytes) {
    auto& registry = ConnectionRegistry::instance();
    auto connections = registry.get_all_connections();

    if (connections.empty()) {
        log_err_train("연결된 추론서버 없음");
        return false;
    }

    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":" << static_cast<int>(ProtocolNo::MODEL_RELOAD_CMD) << ","
       << "\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\","
       << "\"model_type\":\"" << escape_json(model_type) << "\","
       << "\"model_path\":\"" << escape_json(model_path) << "\","
       << "\"version\":\"" << escape_json(version) << "\","
       << "\"image_size\":" << model_bytes.size()
       << "}";
    std::string json_body = os.str();

    bool sent_any = false;
    for (const auto& [addr, fd] : connections) {
        if (!send_json_frame(fd, json_body)) {
            log_err_train("MODEL_RELOAD_CMD JSON 전송 실패 | → %s fd=%d", addr.c_str(), fd);
            continue;
        }
        if (!model_bytes.empty() &&
            !send_all(fd, model_bytes.data(), model_bytes.size())) {
            log_err_train("MODEL_RELOAD_CMD 바이너리 전송 실패 | → %s fd=%d", addr.c_str(), fd);
            continue;
        }
        log_train("MODEL_RELOAD_CMD 전송 성공 | type=%s → %s fd=%d (%zu bytes)",
                  model_type.c_str(), addr.c_str(), fd, model_bytes.size());
        sent_any = true;
    }

    return sent_any;
}

} // namespace factory
