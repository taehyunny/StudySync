// ============================================================================
// gui_notifier.cpp — EventBus 이벤트 → MFC GUI 클라이언트 JSON 푸시
// ============================================================================
// 책임:
//   시스템 내부에서 발생하는 이벤트(NG 검출, 학습 진행률, 서버 장애 등)를
//   연결된 GUI 클라이언트(들)에 실시간 푸시로 전달한다.
//
// 데이터 흐름:
//   [AI/Training Server] → PACKET_RECEIVED → Router → 각종 *_RECEIVED 이벤트
//                                                       ↓
//   [InspectionService] → GUI_PUSH_REQUESTED ────────────── [GuiNotifier]
//   [HealthChecker]     → SERVER_DOWN / SERVER_RECOVERED ──┘      ↓
//                                                            broadcast / broadcast_with_binary
//                                                                  ↓
//                                                          [SessionManager]
//                                                                  ↓
//                                                          모든 GUI 클라이언트
//
// 프로토콜 번호 (클라 방향):
//   110  INSPECT_NG_PUSH         — NG 검출 + 원본/히트맵/마스크 3장 바이너리
//   112  INSPECT_OK_COUNT_PUSH   — 양품/불량 누적 카운트
//   154  RETRAIN_PROGRESS_PUSH   — 재학습 진행률/완료/실패 통합
//   170  SERVER_HEALTH_PUSH      — 서버 down/recovered
//
// station_filter 동작:
//   broadcast(msg, station)      — station_id 일치 또는 0(전체 구독) 인 세션에만
//   broadcast(msg)               — 모든 세션에
//   현재 클라는 station 선택 UI가 없어 전부 0(전체) 구독 상태.
// ============================================================================
#include "session/gui_notifier.h"
#include "session/session_manager.h"
#include "security/json_safety.h"
#include "storage/dao.h"   // v0.15.6: AssemblyDao::extract_array/int 재사용 (Station2 detections 주입)

#include "core/logger.h"

#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>

using factory::security::escape_json;

namespace factory {

GuiNotifier::GuiNotifier(EventBus& bus)
    : event_bus_(bus) {
}

// ---------------------------------------------------------------------------
// register_handlers — 관심 있는 EventType 을 EventBus 에 구독 등록
// main.cpp 에서 서버 부팅 시 1회 호출. 이후 이벤트 발행은 EventBus 의 워커
// 스레드 풀에서 비동기 디스패치된다.
//
// SERVER_DOWN / SERVER_RECOVERED 는 같은 on_server_status 로 라우팅하고
// is_down 플래그로 분기 — 응답 JSON 이 1필드(status)만 다르므로 중복 제거.
// ---------------------------------------------------------------------------
void GuiNotifier::register_handlers() {
    event_bus_.subscribe(EventType::GUI_PUSH_REQUESTED,
                         [this](const std::any& p) { this->on_gui_push(p); });
    event_bus_.subscribe(EventType::SERVER_DOWN,
                         [this](const std::any& p) { this->on_server_status(p, true); });
    event_bus_.subscribe(EventType::SERVER_RECOVERED,
                         [this](const std::any& p) { this->on_server_status(p, false); });
    event_bus_.subscribe(EventType::OK_COUNT_RECEIVED,
                         [this](const std::any& p) { this->on_ok_count(p); });
    event_bus_.subscribe(EventType::TRAIN_PROGRESS_RECEIVED,
                         [this](const std::any& p) { this->on_train_progress(p); });
    event_bus_.subscribe(EventType::TRAIN_COMPLETE_RECEIVED,
                         [this](const std::any& p) { this->on_train_complete(p); });
    event_bus_.subscribe(EventType::TRAIN_FAIL_RECEIVED,
                         [this](const std::any& p) { this->on_train_fail(p); });
}

// NG 검출 시 해당 station 구독자에게만 푸시 (protocol 110)
// v0.9.0+: 원본/히트맵/마스크 3장을 한 번에 전송한다.
// 와이어 포맷 (MFC 클라이언트 파싱 규칙):
//   [4바이트 JSON 길이] + [JSON] + [원본 JPEG] + [히트맵 PNG] + [마스크 PNG]
//   JSON 내 image_size / heatmap_size / pred_mask_size 로 각 크기 전달.
//   크기가 0이면 해당 이미지는 생략(하위호환).
// v0.14.2: 대용량 NG 패킷 rate limit.
// 목적:
//   AI 추론서버가 카메라 미연결 상태에서 더미 이미지로 학습 분포를 벗어난 NG 를
//   초당 여러 건 생성하면, 2~3MB 짜리 패킷이 MFC 로 쏟아져 TCP 프레이밍 경계가
//   어긋나고 CImage 복사 어설션(atlimage.h:1629) 을 유발했다.
//   동일 스테이션의 연속 NG 푸시 간격을 최소 200ms 로 제한하여 버퍼 혼잡 방지.
//
// 정책:
//   - 일반 NG(총 <2MB): 제한 없음 (실운영 부하 보호)
//   - 대용량 NG(≥2MB):  스테이션별 최소 200ms 간격 보장, 초과분 drop + 로그
//   - drop 해도 DB INSERT 는 이미 완료된 뒤라 이력은 보존됨
static bool should_rate_limit_ng_push(int station_id, std::size_t total_bytes) {
    constexpr std::size_t LARGE_THRESHOLD = 2ULL * 1024 * 1024;  // 2 MB
    constexpr int         MIN_INTERVAL_MS = 200;
    if (total_bytes < LARGE_THRESHOLD) return false;  // 작은 패킷은 통과

    // 스테이션 index (1~2) → array slot. 범위 밖은 통과.
    if (station_id < 1 || station_id > 2) return false;

    static std::mutex                                         g_mu;
    static std::array<std::chrono::steady_clock::time_point, 3> g_last{};  // [0]미사용,[1]Station1,[2]Station2

    std::lock_guard<std::mutex> lock(g_mu);
    const auto now = std::chrono::steady_clock::now();
    const auto& last = g_last[station_id];
    if (last.time_since_epoch().count() != 0) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (elapsed_ms < MIN_INTERVAL_MS) {
            return true;  // drop
        }
    }
    g_last[station_id] = now;
    return false;
}

void GuiNotifier::on_gui_push(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    // 대용량 연속 NG rate limit (상세 주석은 should_rate_limit_ng_push 참조)
    const std::size_t total_bin = ev.image_bytes.size()
                                + ev.heatmap_bytes.size()
                                + ev.pred_mask_bytes.size();
    if (should_rate_limit_ng_push(ev.station_id, total_bin)) {
        log_push("NG 푸시 스킵 (rate limit) | 스테이션=%d 크기=%zu bytes",
                 ev.station_id, total_bin);
        return;
    }

    // v0.15.6: Station2 전용 YOLO 디텍션/구조판정 필드를 NG_PUSH JSON 에 포함.
    //   이전엔 detections / cap_ok / label_ok / fill_ok 가 ev.raw_json 에는 있지만
    //   gui_notifier 가 JSON 조립 시 누락 → MFC PageStation2 의 YOLO 리스트 영구 빈 상태.
    //   AssemblyDao::extract_* static 유틸 재사용으로 최소 침습 추가.
    //   Station1(PatchCore 단독) 에서는 해당 키가 raw_json 에 없으므로 안전한 기본값
    //   ("[]" / 0) 으로 주입 — MFC 수신측에 지장 없음.
    std::string detections_arr = (ev.station_id == 2)
        ? AssemblyDao::extract_array(ev.raw_json, "detections") : std::string("[]");
    int cap_ok   = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "cap_ok")   : 0;
    int label_ok = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "label_ok") : 0;
    int fill_ok  = (ev.station_id == 2) ? AssemblyDao::extract_int(ev.raw_json, "fill_ok")  : 0;

    std::ostringstream os;
    os << "{\"protocol_no\":110"
       << ",\"id\":" << ev.db_id                      // v0.14.7: DB row id (MFC 리스트 중복방지 키)
       << ",\"inspection_id\":\"" << escape_json(ev.inspection_id) << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"result\":\"" << escape_json(ev.result) << "\""
       << ",\"defect_type\":\"" << escape_json(ev.defect_type) << "\""
       << ",\"score\":" << ev.score
       << ",\"latency_ms\":" << ev.latency_ms
       << ",\"timestamp\":\"" << escape_json(ev.timestamp) << "\""
       << ",\"image_size\":"     << ev.image_bytes.size()
       << ",\"heatmap_size\":"   << ev.heatmap_bytes.size()
       << ",\"pred_mask_size\":" << ev.pred_mask_bytes.size()
       // v0.15.6: Station2 YOLO 디텍션/구조판정 (Station1 은 "[]"/0)
       << ",\"detections\":" << (detections_arr.empty() ? std::string("[]") : detections_arr)
       << ",\"cap_ok\":"     << cap_ok
       << ",\"label_ok\":"   << label_ok
       << ",\"fill_ok\":"    << fill_ok
       << "}";

    // 세 바이너리를 순서대로 이어붙여 하나의 연속 블록으로 전송.
    // MFC 클라이언트는 JSON에서 각 size를 읽고 offset 계산으로 분리한다.
    // total_bin 은 위 rate limit 체크에서 이미 계산됨.
    if (total_bin > 0) {
        std::vector<uint8_t> combined;
        combined.reserve(total_bin);
        combined.insert(combined.end(), ev.image_bytes.begin(),     ev.image_bytes.end());
        combined.insert(combined.end(), ev.heatmap_bytes.begin(),   ev.heatmap_bytes.end());
        combined.insert(combined.end(), ev.pred_mask_bytes.begin(), ev.pred_mask_bytes.end());
        SessionManager::instance().broadcast_with_binary(os.str(), combined, ev.station_id);
    } else {
        SessionManager::instance().broadcast(os.str(), ev.station_id);
    }
    log_push("NG 푸시 | 스테이션=%d 접속자=%zu명 (원본=%zu 히트맵=%zu 마스크=%zu)",
             ev.station_id, SessionManager::instance().session_count(),
             ev.image_bytes.size(), ev.heatmap_bytes.size(), ev.pred_mask_bytes.size());
}

// 서버 장애/복구 알림 — 전체 클라이언트에 브로드캐스트 (protocol 170)
void GuiNotifier::on_server_status(const std::any& payload, bool is_down) {
    const auto& ev = std::any_cast<const ServerStatusEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":170"
       << ",\"server_name\":\"" << escape_json(ev.server_name) << "\""
       << ",\"ip\":\"" << escape_json(ev.ip) << "\""
       << ",\"port\":" << ev.port
       << ",\"status\":\"" << (is_down ? "down" : "recovered") << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    // v0.14.7: 매 tick 브로드캐스트로 바뀐 뒤 로그 노이즈 급증 방지 — TRACE 수준으로만 출력.
    //   최초/상태전환은 이미 HealthChecker 가 log_main 으로 찍음.
    // log_push 대신 log_push_debug (있으면) 또는 생략.
}

// 양품/불량 집계 카운트 갱신 푸시 (protocol 112)
void GuiNotifier::on_ok_count(const std::any& payload) {
    const auto& ev = std::any_cast<const OkCountEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":112"
       << ",\"station_id\":" << ev.station_id
       << ",\"ok_count\":" << ev.ok_count
       << ",\"ng_count\":" << ev.ng_count
       << ",\"latency_avg\":" << ev.latency_avg
       << ",\"period\":\"" << escape_json(ev.period) << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
}

// 재학습 진행률 푸시 (protocol 154, status="진행중")
void GuiNotifier::on_train_progress(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainProgressEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << escape_json(ev.request_id) << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << escape_json(ev.model_type) << "\""
       << ",\"progress\":" << ev.progress
       << ",\"epoch\":" << ev.epoch
       << ",\"loss\":" << ev.loss
       << ",\"status\":\"" << escape_json(ev.status) << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 진행률 푸시 | 스테이션=%d 진행=%d%%", ev.station_id, ev.progress);
}

// 재학습 완료 푸시 (protocol 154, progress=100, status="완료")
void GuiNotifier::on_train_complete(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainCompleteEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << escape_json(ev.request_id) << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << escape_json(ev.model_type) << "\""
       << ",\"progress\":100"
       << ",\"status\":\"완료\""
       << ",\"version\":\"" << escape_json(ev.version) << "\""
       << ",\"accuracy\":" << ev.accuracy
       << ",\"message\":\"" << escape_json(ev.message) << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 완료 푸시 | 스테이션=%d 버전=%s", ev.station_id, ev.version.c_str());
}

// 재학습 실패 푸시 (protocol 154, progress=-1, status="실패")
void GuiNotifier::on_train_fail(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainFailEvent&>(payload);

    std::ostringstream os;
    os << "{\"protocol_no\":154"
       << ",\"request_id\":\"" << escape_json(ev.request_id) << "\""
       << ",\"station_id\":" << ev.station_id
       << ",\"model_type\":\"" << escape_json(ev.model_type) << "\""
       << ",\"progress\":-1"
       << ",\"status\":\"실패\""
       << ",\"message\":\"" << escape_json(ev.message) << "\""
       << "}";

    SessionManager::instance().broadcast(os.str());
    log_push("학습 실패 푸시 | 스테이션=%d 사유=%s", ev.station_id, ev.message.c_str());
}

} // namespace factory
