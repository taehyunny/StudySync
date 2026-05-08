// ============================================================================
// posture_service.cpp — 자세 도메인 처리 (DB INSERT + ACK)
// ============================================================================
#include "service/posture_service.h"
#include "Protocol.h"
#include "core/logger.h"

namespace factory {

PostureService::PostureService(EventBus& bus, ConnectionPool& pool)
    : event_bus_(bus), log_dao_(pool), event_dao_(pool) {
}

void PostureService::register_handlers() {
    event_bus_.subscribe(EventType::POSTURE_LOG_PUSH_RECEIVED,
                         [this](const std::any& p) { this->on_posture_log_push(p); });
    event_bus_.subscribe(EventType::POSTURE_EVENT_PUSH_RECEIVED,
                         [this](const std::any& p) { this->on_posture_event_push(p); });
    event_bus_.subscribe(EventType::BASELINE_CAPTURE_RECEIVED,
                         [this](const std::any& p) { this->on_baseline_capture(p); });
}

void PostureService::on_posture_log_push(const std::any& payload) {
    const auto& ev = std::any_cast<const PostureLogPushEvent&>(payload);

    PostureLogDao::Entry e{};
    e.session_id        = ev.session_id;
    e.ts                = ev.ts;
    e.timestamp_ms      = ev.timestamp_ms;
    e.has_neck_angle    = ev.has_neck_angle;
    e.neck_angle        = ev.neck_angle;
    e.has_shoulder_diff = ev.has_shoulder_diff;
    e.shoulder_diff     = ev.shoulder_diff;
    e.posture_ok        = ev.posture_ok;
    e.has_vs_baseline   = ev.has_vs_baseline;
    e.vs_baseline       = ev.vs_baseline;

    long long row_id = log_dao_.insert(e);

    AckSendEvent ack{};
    ack.protocol_no = static_cast<int>(ProtocolNo::POSTURE_LOG_ACK);
    ack.request_id  = ev.request_id;
    ack.sender_addr = ev.sender_addr;
    ack.ack_ok      = (row_id > 0);
    if (!ack.ack_ok) ack.error_message = "posture_log_insert_failed";
    if (!event_bus_.publish_critical(EventType::ACK_SEND_REQUESTED, ack,
                                     std::chrono::milliseconds(200), true)) {
        log_err_ack("POSTURE_LOG_ACK 큐잉 실패 | req=%s", ack.request_id.c_str());
    }
}

void PostureService::on_posture_event_push(const std::any& payload) {
    const auto& ev = std::any_cast<const PostureEventPushEvent&>(payload);

    PostureEventDao::Entry e{};
    e.event_id          = ev.event_id;
    e.session_id        = ev.session_id;
    e.event_type        = ev.event_type;
    e.severity          = ev.severity;
    e.reason            = ev.reason;
    e.ts                = ev.ts;
    e.timestamp_ms      = ev.timestamp_ms;
    e.clip_id           = ev.clip_id;
    e.clip_access       = ev.clip_access;
    e.clip_ref          = ev.clip_ref;
    e.clip_format       = ev.clip_format;
    e.frame_count       = ev.frame_count;
    e.retention_days    = ev.retention_days;
    e.has_expires_at_ms = ev.has_expires_at_ms;
    e.expires_at_ms     = ev.expires_at_ms;

    long long row_id = event_dao_.insert(e);

    // 멱등 — event_id 중복이어도 ACK 는 성공으로.
    AckSendEvent ack{};
    ack.protocol_no = static_cast<int>(ProtocolNo::POSTURE_EVENT_ACK);
    ack.request_id  = ev.event_id;        // posture_event 는 event_id 가 매칭 키
    ack.sender_addr = ev.sender_addr;
    ack.ack_ok      = (row_id > 0);
    if (!ack.ack_ok) ack.error_message = "posture_event_insert_failed";
    if (!event_bus_.publish_critical(EventType::ACK_SEND_REQUESTED, ack,
                                     std::chrono::milliseconds(200), true)) {
        log_err_ack("POSTURE_EVENT_ACK 큐잉 실패 | req=%s", ack.request_id.c_str());
    }
}

void PostureService::on_baseline_capture(const std::any& payload) {
    const auto& ev = std::any_cast<const BaselineCaptureEvent&>(payload);

    // 메인서버 저장 여부는 미결정 — 일단 로그만 남기고 ACK.
    log_main("BASELINE_CAPTURE 수신 | user=%lld session=%lld neck=%.2f shoulder=%.2f ts=%s",
             ev.user_id, ev.session_id, ev.neck_angle, ev.shoulder_diff, ev.ts.c_str());

    AckSendEvent ack{};
    ack.protocol_no = static_cast<int>(ProtocolNo::BASELINE_CAPTURE_ACK);
    ack.request_id  = ev.request_id;
    ack.sender_addr = ev.sender_addr;
    ack.ack_ok      = true;
    if (!event_bus_.publish_critical(EventType::ACK_SEND_REQUESTED, ack,
                                     std::chrono::milliseconds(200), true)) {
        log_err_ack("BASELINE_CAPTURE_ACK 큐잉 실패 | req=%s", ack.request_id.c_str());
    }
}

} // namespace factory
