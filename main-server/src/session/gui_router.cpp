// ============================================================================
// gui_router.cpp — GUI 클라이언트 요청 라우터 구현
// ============================================================================
// 책임:
//   TCP 로 들어온 GUI 클라이언트 요청 JSON 을 protocol_no 로 분기하여 맞는
//   handle_* 함수를 호출한다. 각 핸들러는 JSON 필드 추출 → GuiService 호출
//   → 응답 JSON 조립 → 전송 순서로 동작한다.
//
// 3계층 중 "Presentation" 층에 해당:
//   GuiTcpListener → [GuiRouter] → GuiService → DAO
//
// 로깅 정책 (v0.11.0):
//   모든 핸들러 진입부에 `log_clt("~ 요청 수신 | fd=... ...")` 형태로 로그를
//   남긴다. 어느 클라이언트가 어떤 버튼/페이지 전환을 했는지 서버 쪽에서
//   실시간 추적 가능하도록 한다.
//
// 보안:
//   - JSON 파싱은 외부 라이브러리 미사용 (extract_str/extract_int).
//     중첩 객체·배열·이스케이프된 따옴표는 지원하지 않으며, 필드가 문자열 리터럴로
//     플랫하게 나오는 현재 프로토콜에서만 안전하다.
//   - 문자열 길이 상한 512자 (extract_str) — 과도한 메모리 점유 방지.
//   - escape_json 은 security 모듈로 위임하여 제어문자·개행 처리.
// ============================================================================
#include "session/gui_router.h"
#include "session/session_manager.h"
#include "monitor/connection_registry.h"
#include "core/config.h"
#include "core/logger.h"
#include "core/tcp_utils.h"
#include "security/json_safety.h"
#include "Protocol.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace factory {

GuiRouter::GuiRouter(GuiService& service)
    : service_(service) {
}

// ---------------------------------------------------------------------------
// route — GuiTcpListener 가 완성된 JSON 1프레임을 넘길 때마다 호출
//
// 흐름:
//   1) extract_int 로 "protocol_no" 필드만 먼저 파싱 (저비용)
//   2) switch 로 프로토콜 번호 → handle_* 디스패치
//   3) EXT_ACK(주기적 heartbeat) 는 조용히 무시 — 별도 응답 불필요
//   4) 미지의 프로토콜은 경고 로그만 남기고 연결은 유지
//
// 매개변수:
//   client_fd    GUI 클라이언트 소켓 fd (응답 송신 대상)
//   remote_addr  "ip:port" — 미지 프로토콜 진단용
//   json_request [4바이트 길이 헤더 제거 후] 순수 JSON 본문
// ---------------------------------------------------------------------------
void GuiRouter::route(int client_fd, const std::string& remote_addr,
                      const std::string& json_request,
                      const std::vector<uint8_t>& binary) {
    int protocol_no = extract_int(json_request, "protocol_no");

    switch (protocol_no) {
        case static_cast<int>(ProtocolNo::LOGIN_REQ):
            handle_login(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::LOGOUT_REQ):
            handle_logout(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::REGISTER_REQ):
            handle_register(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::INSPECT_HISTORY_REQ):
            handle_inspect_history(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::INSPECT_IMAGE_REQ):
            handle_inspect_image(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::STATS_REQ):
            handle_stats(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::MODEL_LIST_REQ):
            handle_model_list(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::RETRAIN_REQ):
            handle_retrain(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::RETRAIN_UPLOAD):
            // v0.13.0: 학습용 이미지 1장 업로드 (바이너리 동반)
            handle_retrain_upload(client_fd, json_request, binary); break;
        case static_cast<int>(ProtocolNo::INSPECT_CONTROL_REQ):
            // v0.14.0: 검사 pause/resume 요청 → 추론서버에 중계
            handle_inspect_control(client_fd, json_request); break;
        case static_cast<int>(ProtocolNo::EXT_ACK):
            // heartbeat — 너무 빈번해서 로그 생략
            break;
        case static_cast<int>(ProtocolNo::INSPECT_NG_ACK_EXT):
            // NG 푸시 수신 확인 — 클라가 **무사히** 푸시를 디코드한 신호.
            // 이 로그가 "클라이언트 해제" 직전에 찍히면 ACK 보낸 뒤 끊긴 것(정상 경로 중 끊김).
            // 이 로그가 안 찍히고 바로 해제되면 NG 수신 직후 프로세스가 죽었다는 뜻(크래시 의심).
            log_clt("NG ACK 수신 | fd=%d ip=%s", client_fd, remote_addr.c_str());
            break;
        default:
            log_clt("미처리 프로토콜 | no=%d ip=%s", protocol_no, remote_addr.c_str());
            break;
    }
}

// ── LOGIN ────────────────────────────────────────────────────────────

void GuiRouter::handle_login(int fd, const std::string& json) {
    std::string username   = extract_str(json, "username");
    std::string password   = extract_str(json, "password");
    std::string request_id = extract_str(json, "request_id");

    log_clt("로그인 요청 | 사용자=%s", username.c_str());

    auto result = service_.login(username, password);

    if (result.success) {
        // 중복 로그인 처리: 같은 username의 기존 세션이 있으면 강제 종료하여 최신 로그인이 승리.
        // LoginDlg→MainTabDlg 정상 전환, VPN/IP 변경, 멀티PC 모두 동일하게 "새 세션이 이김".
        int existing_fd = SessionManager::instance().find_fd_by_username(username);
        if (existing_fd >= 0 && existing_fd != fd) {
            auto existing_addr = SessionManager::instance().get_remote_addr(existing_fd);
            auto new_addr      = SessionManager::instance().get_remote_addr(fd);
            log_clt("기존 세션 교체 | 사용자=%s 기존=%s 신규=%s",
                    username.c_str(), existing_addr.c_str(), new_addr.c_str());
            // shutdown으로 기존 소켓을 깨워 handle_client 루프가 정상 종료하도록 함
            // (unregister만 하면 소켓이 남아 TIME_WAIT/fd 누수 위험)
            SessionManager::instance().force_close(existing_fd);
        }
        SessionManager::instance().set_client_info(fd, username, 0);
    }

    std::ostringstream os;
    os << "{\"protocol_no\":101"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"username\":\"" << escape_json(username) << "\""
       << ",\"role\":\"" << escape_json(result.role) << "\""
       << ",\"employee_id\":\"" << escape_json(result.employee_id) << "\""
       << ",\"message\":\"" << (result.success ? "로그인 성공" : "인증 실패") << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());

    // ── 로그인 성공 시 현재 서버 상태(LED) 초기 동기화 ──
    // 목적: 클라이언트가 접속한 시점에는 HealthChecker의 "상태 전환" 이벤트를
    //       놓친 상태이므로 초기 LED를 알 수 없다.
    //       config의 health_check 타겟별로 현재 ConnectionRegistry 조회하여
    //       현재 상태(alive/down)를 HEALTH_PUSH(170)로 즉시 전송한다.
    if (result.success) {
        auto& cfg = Config::instance();
        // v0.14.7: 초기 동기화 매칭을 IP prefix → server_type 으로 교체.
        //   config 에 ip="" (dynamic) 로 설정되어 있으면 prefix ":" 로 어떤 주소도 매칭 안 되어
        //   모든 LED 가 down 으로 찍혀 있었음. 이제 ConnectionRegistry 가 태깅한 server_type
        //   (ai_inference_1/2/ai_training) 으로 직접 매칭.
        auto connections = ConnectionRegistry::instance().get_all_connections_detailed();

        for (const auto& target : cfg.get_health_targets()) {
            bool alive = false;
            // 우선 server_type 이 일치하는 연결 탐색
            for (const auto& [addr, info] : connections) {
                if (info.server_type == target.name) { alive = true; break; }
            }
            // 하위호환: ip 가 설정되어 있으면 prefix 매칭도 병행
            if (!alive && !target.ip.empty()) {
                std::string ip_prefix = target.ip + ":";
                for (const auto& [addr, info] : connections) {
                    if (addr.rfind(ip_prefix, 0) == 0) { alive = true; break; }
                }
            }

            // HEALTH_PUSH(170) 전송 — 방금 로그인한 이 클라이언트에게만
            std::ostringstream hp;
            hp << "{\"protocol_no\":170"
               << ",\"server_name\":\"" << escape_json(target.name) << "\""
               << ",\"ip\":\""          << escape_json(target.ip)   << "\""
               << ",\"port\":"          << target.port
               << ",\"status\":\""      << (alive ? "recovered" : "down") << "\""
               << "}";
            send_json(fd, hp.str());
            // v0.14.7: 진단용 — 각 target 의 초기 동기화 결과를 명확히 로그로 남김.
            //   학습서버 LED 가 회색 유지되는 문제 추적용.
            log_clt("초기 HEALTH_PUSH 송신 | target=%s status=%s (fd=%d)",
                    target.name.c_str(), (alive ? "recovered" : "down"), fd);
        }
        log_clt("초기 서버 상태 동기화 완료 | fd=%d", fd);
    }
}

// ── REGISTER ─────────────────────────────────────────────────────────
// 회원가입 요청 처리. UserDao 가 내부에서 bcrypt 해싱 후 INSERT 하므로
// 여기서는 JSON 추출과 응답 조립만 담당.
// ---------------------------------------------------------------------------
void GuiRouter::handle_register(int fd, const std::string& json) {
    std::string username    = extract_str(json, "username");
    std::string password    = extract_str(json, "password");
    std::string employee_id = extract_str(json, "employee_id");
    std::string role        = extract_str(json, "role");
    std::string request_id  = extract_str(json, "request_id");

    log_clt("회원가입 요청 | 사용자=%s 사원ID=%s", username.c_str(), employee_id.c_str());

    auto result = service_.register_user(employee_id, username, password, role);

    std::ostringstream os;
    os << "{\"protocol_no\":105"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── LOGOUT ───────────────────────────────────────────────────────────
// 로그아웃은 실제 세션 정리(SessionManager unregister)가 TCP 연결 종료 시점에
// handle_client 루프 종료 로직에서 수행되므로, 여기서는 클라이언트에게
// "로그아웃 수락" 응답만 돌려준다. 즉 이 핸들러는 "종료 인사" 역할.
// ---------------------------------------------------------------------------
void GuiRouter::handle_logout(int fd, const std::string& json) {
    std::string username = extract_str(json, "username");
    log_clt("로그아웃 | 사용자=%s", username.c_str());

    std::ostringstream os;
    os << "{\"protocol_no\":103"
       << ",\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\""
       << ",\"success\":true"
       << ",\"message\":\"로그아웃 완료\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── INSPECT_HISTORY ──────────────────────────────────────────────────
// 검사 이력 페이지(Stats/History) 에서 기간/스테이션 필터로 조회.
// 이미지 바이너리는 여기서 같이 보내지 않음 — 용량 문제 + 네트워크 효율.
// 사용자가 특정 row 를 클릭해 "상세보기" 하면 INSPECT_IMAGE_REQ(116) 로
// on-demand 로 3장(원본/히트맵/마스크) 만 받아가는 2단계 구조.
// ---------------------------------------------------------------------------
void GuiRouter::handle_inspect_history(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");
    int limit              = extract_int(json, "limit");

    log_clt("검사이력 요청 | fd=%d station=%d from=%s to=%s limit=%d",
            fd, station_filter, date_from.c_str(), date_to.c_str(), limit);

    auto records = service_.get_history(station_filter, date_from, date_to, limit);

    std::ostringstream items;
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << r.id
              << ",\"station_id\":" << r.station_id
              << ",\"timestamp\":\"" << escape_json(r.timestamp) << "\""
              << ",\"result\":\"" << escape_json(r.result) << "\""
              << ",\"confidence\":" << r.confidence
              << ",\"defect_type\":\"" << escape_json(r.defect_type) << "\""
              << ",\"image_path\":\"" << escape_json(r.image_path) << "\""
              << ",\"heatmap_path\":\"" << escape_json(r.heatmap_path) << "\""
              << ",\"pred_mask_path\":\"" << escape_json(r.pred_mask_path) << "\""
              << ",\"latency_ms\":" << r.latency_ms
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":115"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"count\":" << records.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("검사이력 응답 | %zu건", records.size());
}

// ── INSPECT_IMAGE (v0.10+) ─────────────────────────────────────────────
// 이력 상세보기에서 과거 NG 이미지 3장을 on-demand 로 전송.
// 와이어 포맷: [4바이트 JSON 길이] + [JSON] + [원본][히트맵][마스크]
//             NG_PUSH(110)와 동일 규칙. 클라는 기존 RecvLoop 경로로 수신/디코드.
// 보안: 클라는 inspection_id만 보내고, 실제 파일 경로는 서버가 DB로 조회.
//       (클라가 임의 경로를 보내지 못하도록 — 경로 주입 공격 방어)
void GuiRouter::handle_inspect_image(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int inspection_id      = extract_int(json, "inspection_id");

    log_clt("검사이미지 요청 | fd=%d inspection_id=%d", fd, inspection_id);

    auto rec = service_.get_inspection_by_id(inspection_id);

    auto read_file = [](const std::string& path) -> std::vector<uint8_t> {
        std::vector<uint8_t> out;
        if (path.empty()) return out;
        try {
            if (!std::filesystem::exists(path)) return out;
            auto sz = std::filesystem::file_size(path);
            // 50MB 상한 — 비정상 파일로 인한 메모리 폭주 방지
            if (sz == 0 || sz > 50ULL * 1024 * 1024) return out;
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) return out;
            out.resize(static_cast<std::size_t>(sz));
            ifs.read(reinterpret_cast<char*>(out.data()),
                     static_cast<std::streamsize>(sz));
            if (!ifs) { out.clear(); }
        } catch (...) { out.clear(); }
        return out;
    };

    auto img_bytes  = read_file(rec.image_path);
    auto heat_bytes = read_file(rec.heatmap_path);
    auto mask_bytes = read_file(rec.pred_mask_path);

    std::ostringstream os;
    os << "{\"protocol_no\":117"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"inspection_id\":" << inspection_id
       << ",\"station_id\":"    << rec.station_id
       << ",\"success\":" << (rec.id == inspection_id && inspection_id > 0 ? "true" : "false")
       << ",\"image_size\":"     << img_bytes.size()
       << ",\"heatmap_size\":"   << heat_bytes.size()
       << ",\"pred_mask_size\":" << mask_bytes.size()
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    const std::string json_body = os.str();
    const std::size_t total_bin = img_bytes.size() + heat_bytes.size() + mask_bytes.size();

    // 4바이트 길이(BE) + JSON + 3장 바이너리 (NG_PUSH와 동일 프레이밍)
    uint32_t json_len = static_cast<uint32_t>(json_body.size());
    uint8_t hdr[4] = {
        static_cast<uint8_t>((json_len >> 24) & 0xff),
        static_cast<uint8_t>((json_len >> 16) & 0xff),
        static_cast<uint8_t>((json_len >>  8) & 0xff),
        static_cast<uint8_t>( json_len        & 0xff),
    };
    std::vector<uint8_t> frame;
    frame.reserve(4 + json_body.size() + total_bin);
    frame.insert(frame.end(), hdr, hdr + 4);
    frame.insert(frame.end(), json_body.begin(), json_body.end());
    if (!img_bytes.empty())  frame.insert(frame.end(), img_bytes.begin(),  img_bytes.end());
    if (!heat_bytes.empty()) frame.insert(frame.end(), heat_bytes.begin(), heat_bytes.end());
    if (!mask_bytes.empty()) frame.insert(frame.end(), mask_bytes.begin(), mask_bytes.end());

    // send_all 헬퍼로 부분 전송 안전 처리 (tcp_utils.h)
    send_all(fd, frame.data(), frame.size());

    log_clt("이미지 응답 | id=%d img=%zu heat=%zu mask=%zu",
            inspection_id, img_bytes.size(), heat_bytes.size(), mask_bytes.size());
}

// ── STATS ────────────────────────────────────────────────────────────
// 통계 페이지(CPageStats) 에서 기간별 OK/NG 집계 조회. 반환 필드:
//   total, ok/ng count, ng_rate (%), 스테이션별 ok/ng, 평균 지연시간(ms)
// StatsDao 가 단일 집계 SQL 로 수행 — 행 단위로 긁어오지 않음 (성능).
// ---------------------------------------------------------------------------
void GuiRouter::handle_stats(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");
    int station_filter     = extract_int(json, "station_filter");
    std::string date_from  = extract_str(json, "date_from");
    std::string date_to    = extract_str(json, "date_to");

    log_clt("통계 요청 | fd=%d station=%d from=%s to=%s",
            fd, station_filter, date_from.c_str(), date_to.c_str());

    auto s = service_.get_stats(station_filter, date_from, date_to);

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\"protocol_no\":131"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"total\":" << s.total
       << ",\"ok_count\":" << s.ok_count
       << ",\"ng_count\":" << s.ng_count
       << ",\"ng_rate\":" << s.ng_rate
       << ",\"s1_ok\":" << s.s1_ok << ",\"s1_ng\":" << s.s1_ng
       << ",\"s2_ok\":" << s.s2_ok << ",\"s2_ng\":" << s.s2_ng
       << ",\"avg_latency_ms\":" << s.avg_latency
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("통계 응답 | total=%d ng_rate=%.1f%%", s.total, s.ng_rate);
}

// ── MODEL_LIST ───────────────────────────────────────────────────────
// CPageModel 의 모델 목록 조회. `models` 테이블에서 station/type/version/
// accuracy/is_active 필드를 긁어 배열로 반환.
// v0.11.0 이후 model_type 필드가 Station2 PatchCore 구분에 사용됨.
// ---------------------------------------------------------------------------
void GuiRouter::handle_model_list(int fd, const std::string& json) {
    std::string request_id = extract_str(json, "request_id");

    log_clt("모델목록 요청 | fd=%d", fd);

    auto models = service_.get_models();

    std::ostringstream items;
    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        if (i > 0) items << ",";
        items << "{\"id\":" << m.id
              << ",\"station_id\":" << m.station_id
              << ",\"model_type\":\"" << escape_json(m.model_type) << "\""
              << ",\"version\":\"" << escape_json(m.version) << "\""
              << ",\"accuracy\":" << m.accuracy
              << ",\"deployed_at\":\"" << escape_json(m.deployed_at) << "\""
              << ",\"is_active\":" << m.is_active
              << "}";
    }

    std::ostringstream os;
    os << "{\"protocol_no\":151"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"count\":" << models.size()
       << ",\"items\":[" << items.str() << "]"
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
    log_clt("모델목록 응답 | %zu건", models.size());
}

// ── RETRAIN ──────────────────────────────────────────────────────────
// 재학습 요청(152) — GuiService 가 TCP 로 학습서버에 TRAIN_START_REQ(1100)
// 을 포워딩하고, 수락 여부(success) 를 즉시 클라에 응답.
// 실제 학습 진행률/완료는 나중에 다른 경로(TRAIN_PROGRESS/COMPLETE) 로 비동기
// 전달되며 GuiNotifier 가 RETRAIN_PROGRESS_PUSH(154) 로 모든 클라에 브로드캐스트.
// ---------------------------------------------------------------------------
void GuiRouter::handle_retrain(int fd, const std::string& json) {
    std::string request_id   = extract_str(json, "request_id");
    int station_id           = extract_int(json, "station_id");
    std::string model_type   = extract_str(json, "model_type");
    std::string product_name = extract_str(json, "product_name");
    int image_count          = extract_int(json, "image_count");
    std::string session_id   = extract_str(json, "session_id");   // v0.13.0

    log_clt("재학습 요청 수신 | fd=%d station=%d type=%s product=%s 이미지=%d건 session=%s",
            fd, station_id, model_type.c_str(), product_name.c_str(), image_count,
            session_id.c_str());

    auto result = service_.request_retrain(station_id, model_type, product_name,
                                            image_count, request_id, session_id);

    std::ostringstream os;
    os << "{\"protocol_no\":153"
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── RETRAIN_UPLOAD (v0.13.0) ─────────────────────────────────────────
// 클라가 학습용 이미지 1장을 업로드 → MainServer 로컬 저장 + 학습서버로 중계.
// GuiService 가 1) 로컬 저장 2) 학습서버 TCP 중계 3) 결과 수집 까지 수행,
// 여기서는 요청 파싱과 ACK(159) 응답만 담당.
// ---------------------------------------------------------------------------
void GuiRouter::handle_retrain_upload(int fd, const std::string& json,
                                       const std::vector<uint8_t>& binary) {
    std::string request_id = extract_str(json, "request_id");
    std::string session_id = extract_str(json, "session_id");
    int         station_id = extract_int(json, "station_id");
    std::string model_type = extract_str(json, "model_type");
    std::string filename   = extract_str(json, "filename");
    int         file_index = extract_int(json, "file_index");
    int         total_files= extract_int(json, "total_files");

    log_clt("학습 업로드 | fd=%d session=%s station=%d type=%s [%d/%d] %s (%zu bytes)",
            fd, session_id.c_str(), station_id, model_type.c_str(),
            file_index + 1, total_files, filename.c_str(), binary.size());

    auto result = service_.receive_retrain_upload(
        session_id, station_id, model_type, filename, binary);

    std::ostringstream os;
    os << "{\"protocol_no\":" << static_cast<int>(ProtocolNo::RETRAIN_UPLOAD_ACK)
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"session_id\":\"" << escape_json(session_id) << "\""
       << ",\"file_index\":" << file_index
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"saved_path\":\"" << escape_json(result.saved_path) << "\""
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── INSPECT_CONTROL (v0.14.0) ────────────────────────────────────────
// 클라 메뉴 [검사 시작/중지] → 모든(또는 지정) 추론서버에 pause/resume 중계.
// ---------------------------------------------------------------------------
void GuiRouter::handle_inspect_control(int fd, const std::string& json) {
    std::string request_id     = extract_str(json, "request_id");
    int         station_filter = extract_int(json, "station_filter");
    std::string action         = extract_str(json, "action");

    log_clt("검사 제어 요청 | fd=%d station=%d action=%s",
            fd, station_filter, action.c_str());

    auto result = service_.inspect_control(station_filter, action, request_id);

    std::ostringstream os;
    os << "{\"protocol_no\":" << static_cast<int>(ProtocolNo::INSPECT_CONTROL_RES)
       << ",\"request_id\":\"" << escape_json(request_id) << "\""
       << ",\"action\":\"" << escape_json(action) << "\""
       << ",\"success\":" << (result.success ? "true" : "false")
       << ",\"applied_count\":" << result.applied_count
       << ",\"message\":\"" << escape_json(result.message) << "\""
       << ",\"timestamp\":\"" << get_timestamp() << "\"}";

    send_json(fd, os.str());
}

// ── 유틸리티 ─────────────────────────────────────────────────────────
// 여러 handle_* 에서 공통으로 쓰이는 경량 헬퍼들.
// 외부 JSON 라이브러리 없이 문자열 처리만으로 구성 (의존성 최소화).
// ---------------------------------------------------------------------------

// 현재 로컬 시각을 ISO8601 초단위로. 응답 JSON 의 "timestamp" 필드용.
// localtime_r / localtime_s 분기: Linux 배포가 기본이지만 Windows 빌드 호환도 유지.
std::string GuiRouter::get_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);  // thread-safe 변형 (localtime 은 TLS 공유)
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

// JSON 값 escape. security 모듈의 통합 구현에 위임 —
// 제어문자(\n, \t, \\, \") 및 UTF-8 유효성 처리 모두 포함.
std::string GuiRouter::escape_json(const std::string& s) {
    return factory::security::escape_json(s);
}

// 4바이트 BE 길이 + JSON 본문 프레이밍으로 송신 (partial send 자동 재시도).
bool GuiRouter::send_json(int fd, const std::string& json_body) {
    return send_json_frame(fd, json_body);
}

// ---------------------------------------------------------------------------
// extract_str — "key":"value" 에서 value 문자열 추출
// 한계:
//   - 중첩 객체/배열 미지원 — 1-depth 평탄 JSON 전용
// 보안:
//   512자 상한 — 비정상 대용량 입력으로 인한 메모리 폭주 방지
// v0.15.2 하드닝:
//   MFC CPacketBuilder::ExtractString 과 동일한 **백슬래시 카운팅 로직** 적용.
//   `\\"` 같이 백슬래시가 짝수 개 연속되면 그 뒤의 `"` 는 진짜 종료 따옴표이고,
//   홀수 개면 escape 된 따옴표. 이전 `lq = find('"', fq+1)` 구현은 모든 escape 를
//   무시해 조기 종료하는 버그가 있었음(현재 프로토콜은 escape 드물어 실질 영향은 낮았음).
// ---------------------------------------------------------------------------
std::string GuiRouter::extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto fq = json.find('"', colon);        // 값의 시작 따옴표
    if (fq == std::string::npos) return "";

    // 종료 따옴표 탐색 (백슬래시 카운팅)
    std::size_t lq = std::string::npos;
    for (std::size_t i = fq + 1; i < json.size(); ++i) {
        if (json[i] == '"') {
            std::size_t bs = 0;
            for (std::size_t j = i; j > fq + 1 && json[j - 1] == '\\'; --j) ++bs;
            if ((bs % 2) == 0) { lq = i; break; }
        }
    }
    if (lq == std::string::npos) return "";

    std::string value = json.substr(fq + 1, lq - fq - 1);
    if (value.size() > 512) value.resize(512);
    return value;
}

// ---------------------------------------------------------------------------
// extract_int — "key":123 에서 정수 추출
// strtol: 실패 시 0 반환(endptr 미사용) — 필드 없으면 기본값 0 으로 처리하는 것과 동일.
// ---------------------------------------------------------------------------
int GuiRouter::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    // strtol 은 선행 공백을 자동으로 건너뜀 — colon+1 부터 바로 파싱
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

} // namespace factory
