// ============================================================================
// router.cpp — 패킷 라우팅 (프로토콜 번호별 이벤트 분기)
// ============================================================================
// 목적:
//   AI 추론서버/학습서버로부터 수신한 패킷을 protocol_no 기준으로 분류하여
//   적절한 EventType으로 재발행한다.
//
// 패킷 수명 흐름:
//   PacketReader → PACKET_RECEIVED → [Router] → INSPECTION_INBOUND
//                                              → INSPECTION_ASSEMBLY
//                                              → OK_COUNT_RECEIVED
//                                              → INSPECT_META_RECEIVED
//                                              → TRAIN_PROGRESS_RECEIVED
//                                              → TRAIN_COMPLETE_RECEIVED
//                                              → TRAIN_FAIL_RECEIVED
// ============================================================================
#include "handler/router.h"
#include "Protocol.h"
#include "monitor/connection_registry.h"

#include "core/logger.h"

#include <cstdlib>
#include <iostream>

namespace factory {

Router::Router(EventBus& bus)
    : event_bus_(bus) {
}

// ---------------------------------------------------------------------------
// register_handlers — PACKET_RECEIVED 이벤트 구독을 등록한다.
// 서버 부팅 시 1회 호출하며, 이후 모든 수신 패킷이 on_packet_received로 흐른다.
// ---------------------------------------------------------------------------
void Router::register_handlers() {
    event_bus_.subscribe(EventType::PACKET_RECEIVED,
                         [this](const std::any& p) { this->on_packet_received(p); });
}

// ---------------------------------------------------------------------------
// on_packet_received — 수신 패킷의 protocol_no에 따라 도메인 이벤트로 변환·발행.
//
// 각 case 블록은 JSON 필드를 추출해 타입 안전한 이벤트 구조체를 채운 뒤
// EventBus에 발행한다. Router는 비즈니스 로직을 일절 수행하지 않는다.
// ---------------------------------------------------------------------------
void Router::on_packet_received(const std::any& payload) {
    const auto& packet = std::any_cast<const PacketReceivedEvent&>(payload);

    int protocol_no = extract_int(packet.json_payload, "protocol_no");
    auto no = static_cast<ProtocolNo>(protocol_no);

    // v0.11.0: 연결에 server_type 태깅 (HealthChecker 동적 감지용)
    //   station_id 가 있는 패킷 → "ai_inference_{N}"
    //   학습 관련 패킷         → "ai_training"
    //   HEALTH_PONG            → JSON 의 server_type 필드 그대로
    // 이미 태깅되어 있으면 set_server_type 내부에서 noop — 매 패킷 호출 안전.
    {
        int st = extract_int(packet.json_payload, "station_id");
        std::string stype;
        if (no == ProtocolNo::TRAIN_PROGRESS || no == ProtocolNo::TRAIN_COMPLETE
            || no == ProtocolNo::TRAIN_FAIL) {
            stype = "ai_training";
        } else if (no == ProtocolNo::HEALTH_PONG) {
            std::string srv = extract_str(packet.json_payload, "server_type");
            if (srv == "training")      stype = "ai_training";
            else if (srv == "station1") stype = "ai_inference_1";
            else if (srv == "station2") stype = "ai_inference_2";
            else if (st == 1)           stype = "ai_inference_1";
            else if (st == 2)           stype = "ai_inference_2";
        } else if (st == 1) {
            stype = "ai_inference_1";
        } else if (st == 2) {
            stype = "ai_inference_2";
        }
        if (!stype.empty() && !packet.remote_addr.empty()) {
            ConnectionRegistry::instance().set_server_type(packet.remote_addr, stype);
        }
    }

    switch (no) {
        // ── NG 검사 결과 (Station1: 입고, Station2: 조립) ──────────────
        // 이미지 바이너리가 함께 올 수 있으므로 image_bytes도 전달한다.
        case ProtocolNo::STATION1_NG:
        case ProtocolNo::STATION2_NG: {
            InspectionEvent ev{};
            ev.protocol_no    = protocol_no;
            ev.inspection_id  = extract_str(packet.json_payload, "inspection_id");
            ev.station_id     = extract_int(packet.json_payload, "station_id");
            ev.result         = extract_str(packet.json_payload, "result");
            ev.defect_type    = extract_str(packet.json_payload, "defect");
            ev.score          = extract_double(packet.json_payload, "score");
            ev.latency_ms     = extract_int(packet.json_payload, "latency_ms");
            ev.timestamp      = extract_str(packet.json_payload, "timestamp");
            ev.image_bytes      = packet.image_bytes;
            ev.heatmap_bytes    = packet.heatmap_bytes;    // v0.9.0+ 히트맵 오버레이 PNG
            ev.pred_mask_bytes  = packet.pred_mask_bytes;  // v0.9.0+ Pred Mask 오버레이 PNG
            ev.raw_json       = packet.json_payload;
            ev.sender_addr    = packet.remote_addr;

            // Station1이면 입고검사, Station2이면 조립검사 이벤트로 분기
            if (no == ProtocolNo::STATION1_NG) {
                event_bus_.publish(EventType::INSPECTION_INBOUND, ev);
            } else {
                event_bus_.publish(EventType::INSPECTION_ASSEMBLY, ev);
            }
            break;
        }

        // ── 정상(OK) 카운트 집계 ──────────────────────────────────────
        // 추론서버가 주기적으로 보내는 OK/NG 건수 요약 — GUI 대시보드용
        case ProtocolNo::STATION_OK_COUNT: {
            OkCountEvent ev{};
            ev.station_id  = extract_int(packet.json_payload, "station_id");
            ev.ok_count    = extract_int(packet.json_payload, "ok_count");
            ev.ng_count    = extract_int(packet.json_payload, "ng_count");
            ev.latency_avg = extract_double(packet.json_payload, "latency_avg");
            ev.period      = extract_str(packet.json_payload, "period");
            event_bus_.publish(EventType::OK_COUNT_RECEIVED, ev);
            break;
        }

        // ── 검사 메타데이터 (모델 버전 등) ────────────────────────────
        case ProtocolNo::INSPECT_META: {
            InspectMetaEvent ev{};
            ev.inspection_id = extract_str(packet.json_payload, "inspection_id");
            ev.station_id    = extract_int(packet.json_payload, "station_id");
            ev.timestamp     = extract_str(packet.json_payload, "timestamp");
            ev.latency_ms    = extract_int(packet.json_payload, "latency_ms");
            ev.model_id      = extract_int(packet.json_payload, "model_id");
            ev.result        = extract_str(packet.json_payload, "result");
            event_bus_.publish(EventType::INSPECT_META_RECEIVED, ev);
            break;
        }

        // ── Health Check 응답 ─────────────────────────────────────────
        // HealthChecker가 별도 채널을 쓴다면 여기서 흡수 (무시)
        case ProtocolNo::HEALTH_PONG:
            break;

        // ── 모델 리로드 응답 ──────────────────────────────────────────
        case ProtocolNo::MODEL_RELOAD_RES:
            log_ai("모델 리로드 응답 수신");
            break;

        // ── 검사 제어 응답 (v0.14.0) ────────────────────────────────────
        case ProtocolNo::INFERENCE_CONTROL_RES: {
            int st = extract_int(packet.json_payload, "station_id");
            std::string action = extract_str(packet.json_payload, "action");
            log_ai("검사 제어 응답 수신 | station=%d action=%s",
                   st, action.c_str());
            break;
        }

        // ── 학습 진행률 (epoch/loss 등 실시간 업데이트) ────────────────
        case ProtocolNo::TRAIN_PROGRESS: {
            TrainProgressEvent ev{};
            ev.request_id  = extract_str(packet.json_payload, "request_id");
            ev.station_id  = extract_int(packet.json_payload, "station_id");
            ev.model_type  = extract_str(packet.json_payload, "model_type");
            ev.progress    = extract_int(packet.json_payload, "progress");
            ev.epoch       = extract_int(packet.json_payload, "epoch");
            ev.loss        = extract_double(packet.json_payload, "loss");
            ev.status      = extract_str(packet.json_payload, "status");
            ev.sender_addr = packet.remote_addr;
            event_bus_.publish(EventType::TRAIN_PROGRESS_RECEIVED, ev);
            break;
        }

        // ── 학습 완료 (모델 경로·정확도 포함) ─────────────────────────
        case ProtocolNo::TRAIN_COMPLETE: {
            TrainCompleteEvent ev{};
            ev.request_id  = extract_str(packet.json_payload, "request_id");
            ev.station_id  = extract_int(packet.json_payload, "station_id");
            ev.model_type  = extract_str(packet.json_payload, "model_type");
            ev.model_path  = extract_str(packet.json_payload, "model_path");
            ev.version     = extract_str(packet.json_payload, "version");
            ev.accuracy    = extract_double(packet.json_payload, "accuracy");
            ev.message     = extract_str(packet.json_payload, "message");
            ev.sender_addr = packet.remote_addr;
            ev.model_bytes = packet.image_bytes;  // 모델 파일 바이너리 전달
            event_bus_.publish(EventType::TRAIN_COMPLETE_RECEIVED, ev);
            break;
        }

        // ── 학습 실패 (에러 코드·메시지 포함) ─────────────────────────
        case ProtocolNo::TRAIN_FAIL: {
            TrainFailEvent ev{};
            ev.request_id  = extract_str(packet.json_payload, "request_id");
            ev.station_id  = extract_int(packet.json_payload, "station_id");
            ev.model_type  = extract_str(packet.json_payload, "model_type");
            ev.error_code  = extract_str(packet.json_payload, "error_code");
            ev.message     = extract_str(packet.json_payload, "message");
            ev.version     = extract_str(packet.json_payload, "version");
            ev.sender_addr = packet.remote_addr;
            event_bus_.publish(EventType::TRAIN_FAIL_RECEIVED, ev);
            break;
        }

        default:
            log_err_route("미처리 프로토콜 | no=%d", protocol_no);
            break;
    }
}

// ===========================================================================
// 경량 JSON 필드 추출 유틸리티
// ---------------------------------------------------------------------------
// nlohmann::json 등 외부 라이브러리를 사용하지 않는 이유:
//   1. 빌드 의존성 최소화 (임베디드 환경 고려)
//   2. 패킷 JSON이 1-depth 평탄 구조이므로 수동 파싱으로 충분
//   3. 추출 대상 필드가 한정적이어서 full parser 불필요
//
// 한계: 중첩 객체·배열·이스케이프된 따옴표는 지원하지 않는다.
// ===========================================================================

std::string Router::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto first_quote = json.find('"', colon);
    if (first_quote == std::string::npos) return "";
    auto last_quote = json.find('"', first_quote + 1);
    if (last_quote == std::string::npos) return "";
    return json.substr(first_quote + 1, last_quote - first_quote - 1);
}

int Router::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    // strtol은 선행 공백을 자동으로 건너뛰므로 colon+1부터 바로 변환 가능
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

double Router::extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + colon + 1, nullptr);
}

} // namespace factory
