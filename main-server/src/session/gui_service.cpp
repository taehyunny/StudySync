// ============================================================================
// gui_service.cpp — GUI 클라이언트 요청 비즈니스 로직 구현
// ============================================================================
// 목적:
//   GuiRouter 가 파싱해서 넘겨주는 클라이언트 요청(로그인/회원가입/이력/통계/
//   모델목록/재학습)을 실제로 처리한다. DB 접근은 DAO 3종(User/Model/Stats)에
//   위임하고, 학습서버로의 전달은 별도 TCP 연결을 맺어 원샷으로 수행한다.
//
// 책임 분리:
//   GuiRouter → (프로토콜 파싱/응답 JSON 조립)
//   GuiService → (비즈니스 규칙 + DAO 호출 + 학습서버 중계)
//   DAO       → (SQL 실행)
//
// 멀티스레드 주의:
//   - `is_training_` 은 mutex 로 보호 — 동시 재학습 요청 중복 수락 방지.
//   - DAO 는 ConnectionPool 을 통해 스레드당 커넥션을 acquire 하므로 락 불필요.
// ============================================================================
#include "session/gui_service.h"
#include "session/session_manager.h"
#include "monitor/connection_registry.h"
#include "core/logger.h"
#include "core/tcp_utils.h"
#include "Protocol.h"
#include "security/json_safety.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace factory {

// ---------------------------------------------------------------------------
// 생성자 — DAO 3종을 ConnectionPool 로 초기화하고 학습서버 주소를 보관한다.
//   train_host / train_port : config.json 의 network.training_server_host/port
//   (환경변수 TRAIN_HOST 가 있으면 main.cpp 에서 이미 치환되어 들어온다)
// ---------------------------------------------------------------------------
GuiService::GuiService(ConnectionPool& pool,
                       const std::string& train_host,
                       uint16_t train_port)
    : user_dao_(pool), model_dao_(pool), stats_dao_(pool),
      train_host_(train_host), train_port_(train_port) {
}

// ---------------------------------------------------------------------------
// login — username/password 검증 후 LoginResult 반환
//   1) UserDao::find_by_username 으로 DB 에서 해시/권한 조회
//   2) PasswordHash::verify(평문, 해시) — bcrypt 비교 (상수시간 비교)
//   3) 성공 시 last_login 타임스탬프 갱신
//
//   주의: 실패한 경우에도 "사용자 없음" / "비밀번호 불일치" 를 구분하지 않는다.
//         사용자 열거 공격(user enumeration) 방지를 위한 의도된 동작.
// ---------------------------------------------------------------------------
LoginResult GuiService::login(const std::string& username, const std::string& password) {
    LoginResult result;
    auto user = user_dao_.find_by_username(username);

    if (user.found && PasswordHash::verify(password, user.password_hash)) {
        result.success     = true;
        result.username    = username;
        result.role        = user.role;
        result.employee_id = user.employee_id;
        user_dao_.update_last_login(username);
        log_clt("로그인 성공 | 사용자=%s 권한=%s", username.c_str(), user.role.c_str());
    } else {
        // 실패 경로: 일부러 사유를 구분하지 않음 (enumeration 방어)
        log_err_clt("로그인 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ---------------------------------------------------------------------------
// register_user — 신규 사용자 등록
//   선검증(중복) → insert(내부에서 bcrypt 해싱) 순서.
//   UserDao::insert 는 성공 시 true, DB 오류/중복 등은 false.
// ---------------------------------------------------------------------------
RegisterResult GuiService::register_user(const std::string& employee_id,
                                          const std::string& username,
                                          const std::string& password,
                                          const std::string& role) {
    RegisterResult result;

    if (user_dao_.exists(username)) {
        result.message = "이미 존재하는 사용자입니다.";
    } else if (user_dao_.insert(employee_id, username, password, role)) {
        result.success = true;
        result.message = "회원가입 성공";
        log_clt("회원가입 성공 | 사용자=%s", username.c_str());
    } else {
        // DAO 내부에서 구체적 에러는 이미 로그로 남김
        result.message = "DB 오류";
        log_err_clt("회원가입 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ---------------------------------------------------------------------------
// 조회(read-only) 함수들 — StatsDao/ModelDao 에 단순 위임
// 재학습 요청과 달리 상태 변경이 없으므로 mutex 불필요.
// ---------------------------------------------------------------------------
std::vector<StatsDao::InspectionRecord> GuiService::get_history(
    int station_filter, const std::string& from,
    const std::string& to, int limit) {
    return stats_dao_.get_history(station_filter, from, to, limit);
}

StatsDao::StatsResult GuiService::get_stats(
    int station_filter, const std::string& from, const std::string& to) {
    return stats_dao_.get_stats(station_filter, from, to);
}

std::vector<ModelDao::ModelInfo> GuiService::get_models() {
    return model_dao_.list_all();
}

// ---------------------------------------------------------------------------
// make_timestamp — 현재 로컬 시각을 ISO8601(초 단위)로 생성
//   학습서버로 넘기는 TRAIN_START_REQ 패킷의 `timestamp` 필드에 사용.
//   밀리초 미포함: 학습 요청은 초 단위 해상도로 충분.
// ---------------------------------------------------------------------------
static std::string make_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);  // localtime_r: thread-safe (localtime 은 TLS 공유)
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

// send_json_raw 는 tcp_utils.h 의 send_json_frame 으로 대체됨 (partial send 재시도 포함)

// ---------------------------------------------------------------------------
// request_retrain — RETRAIN_REQ(152) 를 학습서버로 TRAIN_START_REQ(1100) 포워딩
//
// 설계 이유 (별도 TCP 커넥션을 맺는 구조):
//   학습서버는 AI 추론서버와 달리 메인에 상시 접속해 있지 않고, 필요 시점에만
//   짧게 통신한다. 한 번 연결 → JSON 1회 송신 → 즉시 close 하는 원샷 패턴.
//   완료/진행률 알림은 학습서버가 "다른 방향"으로 메인에 접속하여 송신한다
//   (AckSender 쪽 비동기 수신 경로).
//
// 동시성 정책:
//   is_training_ 플래그로 동시에 하나만 허용. 두 번째 요청은 즉시 거부하여
//   GPU 메모리/학습 파이프라인 충돌을 방지한다.
//   성공 응답 후 플래그 해제는 TRAIN_COMPLETE/TRAIN_FAIL 수신 핸들러가 담당
//   (여기서는 해제하지 않음 — 학습 진행 중 상태 유지).
// ---------------------------------------------------------------------------
RetrainResult GuiService::request_retrain(int station_id, const std::string& model_type,
                                           const std::string& product_name, int image_count,
                                           const std::string& request_id,
                                           const std::string& session_id) {
    RetrainResult result;

    // ── 1) 동시 학습 방지 — mutex 로 is_training_ 원자 검사/설정 ──
    {
        std::lock_guard<std::mutex> lock(train_mutex_);
        if (is_training_) {
            result.message = "이미 학습이 진행 중입니다.";
            log_err_train("재학습 거부 | 이미 진행 중");
            return result;
        }
        is_training_ = true;  // 이후 실패 경로에서 반드시 해제해야 함
    }

    log_train("재학습 요청 접수 | 스테이션=%d 모델=%s 이미지=%d건",
              station_id, model_type.c_str(), image_count);

    // ── 2) 학습서버 TCP 소켓 생성 ──
    int train_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (train_fd < 0) {
        // 소켓 생성 실패 — 학습은 시작조차 못 함. 플래그 해제 필수.
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
        result.message = "소켓 생성 실패";
        return result;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(train_port_);
    inet_pton(AF_INET, train_host_.c_str(), &addr.sin_addr);

    // 송신 타임아웃 3초 — 학습서버가 멈춰있을 때 클라이언트 응답 지연 최소화
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(train_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // ── 3) 학습서버 접속 시도 — 실패 시 is_training_ 해제 필수 ──
    if (::connect(train_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        log_err_train("학습서버 연결 실패 | %s:%d", train_host_.c_str(), train_port_);
        ::close(train_fd);
        {
            std::lock_guard<std::mutex> lock(train_mutex_);
            is_training_ = false;  // 재시도 가능 상태로 되돌림
        }
        result.message = "학습서버 연결 실패";
        return result;
    }

    // ── 4) TRAIN_START_REQ(1100) JSON 조립 ──
    // v0.13.0: session_id 가 있으면 data_path 를 학습서버 로컬 업로드 경로로 명시.
    //   학습서버의 _train_patchcore/_train_yolo 가 data_path 가 채워져 있으면
    //   기본 경로 대신 이 값을 사용하도록 구현되어 있음 (TrainingMain.py).
    std::string data_path;
    if (!session_id.empty()) {
        // 학습서버 로컬 기준 경로 — TrainingMain 의 _handle_train_data_upload 가
        // ./data/station{N}/uploads/{session_id}/ 로 저장하므로 동일 경로 지정.
        data_path = "./data/station" + std::to_string(station_id)
                    + "/uploads/" + session_id;
    }

    std::ostringstream os;
    os << "{\"protocol_no\":1100"
       << ",\"protocol_version\":\"1.0\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"product_name\":\"" << product_name << "\""
       << ",\"image_count\":" << image_count
       << ",\"data_path\":\"" << data_path << "\""
       << ",\"session_id\":\"" << session_id << "\""
       << ",\"timestamp\":\"" << make_timestamp() << "\"}";

    // ── 5) 원샷 송신 후 close ──
    // send_json_frame: [4바이트 BE 길이] + [JSON] 프레이밍 + partial send 재시도.
    if (send_json_frame(train_fd, os.str())) {
        result.success = true;
        result.message = "재학습 요청 전달 완료";
        log_train("TRAIN_START_REQ → 학습서버 전송 성공");
        // is_training_ 유지 — TRAIN_COMPLETE/FAIL 수신 핸들러가 해제
    } else {
        result.message = "학습서버 전송 실패";
        log_err_train("TRAIN_START_REQ → 학습서버 전송 실패");
        // 전송 실패는 학습 시작 자체가 안 된 것이므로 플래그 해제
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
    }

    ::close(train_fd);
    return result;
}

// ---------------------------------------------------------------------------
// inspect_control (v0.14.0) — 검사 pause/resume 중계
//
// 각 추론서버에 이미 상시 연결된 TCP 소켓을 통해 INFERENCE_CONTROL_CMD(1020) 송신.
// station_filter:
//   0 → server_type == "ai_inference_1" 또는 "ai_inference_2" 모두
//   1 → "ai_inference_1" 만
//   2 → "ai_inference_2" 만
//
// 응답(1021) 은 ConnectionRegistry 의 수신 경로로 돌아와 Router::handle
// 가 로그로만 처리 (클라이언트에는 중계 성공 여부만 INSPECT_CONTROL_RES 로 전달).
// ---------------------------------------------------------------------------
InspectControlResult GuiService::inspect_control(int station_filter,
                                                  const std::string& action,
                                                  const std::string& request_id)
{
    InspectControlResult out;

    // 액션 유효성
    if (action != "pause" && action != "resume") {
        out.message = "invalid_action";
        return out;
    }

    // JSON 조립 — 동일 JSON 을 대상 station 들에 반복 송신
    std::ostringstream os;
    os << "{\"protocol_no\":" << static_cast<int>(ProtocolNo::INFERENCE_CONTROL_CMD)
       << ",\"protocol_version\":\"1.0\""
       << ",\"request_id\":\"" << factory::security::escape_json(request_id) << "\""
       << ",\"action\":\"" << action << "\""
       << ",\"image_size\":0"   // 바이너리 없음 명시
       << "}";
    const std::string json_body = os.str();

    // 대상 연결 결정 (ConnectionRegistry 의 server_type 기준)
    auto connections = ConnectionRegistry::instance().get_all_connections_detailed();
    int sent = 0;
    for (const auto& [addr, info] : connections) {
        const std::string& st = info.server_type;
        bool match = false;
        if (station_filter == 0) {
            match = (st == "ai_inference_1" || st == "ai_inference_2");
        } else if (station_filter == 1) {
            match = (st == "ai_inference_1");
        } else if (station_filter == 2) {
            match = (st == "ai_inference_2");
        }
        if (!match) continue;

        if (send_json_frame(info.fd, json_body)) {
            ++sent;
            log_train("검사 제어 송신 | fd=%d %s action=%s",
                      info.fd, addr.c_str(), action.c_str());
        } else {
            log_err_train("검사 제어 송신 실패 | fd=%d %s", info.fd, addr.c_str());
        }
    }

    out.applied_count = sent;
    out.success = (sent > 0);
    if (!out.success) {
        out.message = "no_inference_server_connected";
    }
    return out;
}

// ---------------------------------------------------------------------------
// receive_retrain_upload (v0.13.0) — 클라 업로드 이미지 1장 처리
//
// 처리 순서:
//   1) 입력 검증 (session_id/filename path traversal, 크기 상한)
//   2) MainServer 로컬 저장 (./storage/training_upload/{session_id}/{filename})
//      — 감사/복구 용도. 저장 실패해도 학습서버 전달은 시도.
//   3) 학습서버로 TCP 중계:
//      · 접속 → TRAIN_DATA_UPLOAD(1108) 송신 (JSON + 이미지 바이트)
//      · ACK(1109) 수신 대기 (최대 10초)
//      · close
//   4) 학습서버 응답을 그대로 RetrainUploadResult 에 담아 반환
//
// 구조 주의:
//   여기서는 매 파일마다 새 TCP 를 여닫는다 (request_retrain 과 동일한 패턴).
//   단순하지만 연결 오버헤드가 있으므로, 고빈도 업로드 시 향후 지속 연결로 전환 여지.
// ---------------------------------------------------------------------------
RetrainUploadResult GuiService::receive_retrain_upload(
    const std::string& session_id,
    int station_id,
    const std::string& model_type,
    const std::string& filename,
    const std::vector<uint8_t>& image_bytes)
{
    RetrainUploadResult out;

    // ── 1) 입력 검증 ──
    if (session_id.empty() || session_id.size() > 64 ||
        session_id.find("..") != std::string::npos ||
        session_id.find('/')  != std::string::npos ||
        session_id.find('\\') != std::string::npos) {
        out.message = "invalid_session_id";
        return out;
    }
    if (station_id != 1 && station_id != 2) {
        out.message = "invalid_station_id";
        return out;
    }
    if (filename.empty() || filename.size() > 128) {
        out.message = "invalid_filename";
        return out;
    }
    // filesystem::path().filename() 로 basename 만 뽑아 경로 탐색 차단
    std::string safe_name = std::filesystem::path(filename).filename().string();
    if (safe_name.empty() || safe_name == "." || safe_name == "..") {
        out.message = "invalid_filename";
        return out;
    }
    if (image_bytes.empty() || image_bytes.size() > 50ULL * 1024 * 1024) {
        out.message = "invalid_image_size";
        return out;
    }

    // ── 2) 로컬 저장 (./storage/training_upload/{session}/{file}) ──
    //     실패는 경고 레벨로만 남기고 학습서버 전달은 계속 시도.
    try {
        std::filesystem::path dir = std::filesystem::path("./storage/training_upload")
                                  / session_id;
        std::filesystem::create_directories(dir);
        std::filesystem::path local_path = dir / safe_name;
        std::ofstream ofs(local_path, std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char*>(image_bytes.data()),
                      static_cast<std::streamsize>(image_bytes.size()));
        } else {
            log_warn("CLT", "학습 업로드 로컬 저장 실패 | %s", local_path.c_str());
        }
    } catch (const std::exception& exc) {
        log_warn("CLT", "학습 업로드 로컬 저장 예외 | %s", exc.what());
    }

    // ── 3) 학습서버 TCP 중계 ──
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        out.message = "socket_create_failed";
        return out;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(train_port_);
    inet_pton(AF_INET, train_host_.c_str(), &addr.sin_addr);

    // 송/수신 타임아웃 10초 — 큰 파일 전송 고려
    struct timeval tv{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        log_err_train("학습서버 연결 실패 | upload session=%s file=%s",
                      session_id.c_str(), safe_name.c_str());
        ::close(fd);
        out.message = "train_server_connect_failed";
        return out;
    }

    // JSON 조립
    std::ostringstream os;
    os << "{\"protocol_no\":" << static_cast<int>(ProtocolNo::TRAIN_DATA_UPLOAD)
       << ",\"protocol_version\":\"1.0\""
       << ",\"session_id\":\""  << factory::security::escape_json(session_id)  << "\""
       << ",\"station_id\":"    << station_id
       << ",\"model_type\":\""  << factory::security::escape_json(model_type)  << "\""
       << ",\"filename\":\""    << factory::security::escape_json(safe_name)   << "\""
       << ",\"image_size\":"    << image_bytes.size()
       << "}";
    const std::string json_body = os.str();

    // [4B length][JSON] 프레이밍으로 헤더+JSON 송신
    if (!send_json_frame(fd, json_body)) {
        ::close(fd);
        out.message = "train_upload_json_send_failed";
        return out;
    }
    // 이미지 바이너리 송신
    if (!image_bytes.empty() &&
        !send_all(fd, image_bytes.data(), image_bytes.size())) {
        ::close(fd);
        out.message = "train_upload_binary_send_failed";
        return out;
    }

    // ACK(1109) 수신
    uint8_t hdr[4];
    auto recv_n_bytes = [](int sock, void* buf, std::size_t n) -> bool {
        std::size_t total = 0;
        auto* p = static_cast<char*>(buf);
        while (total < n) {
            ssize_t got = ::recv(sock, p + total, n - total, 0);
            if (got <= 0) return false;
            total += static_cast<std::size_t>(got);
        }
        return true;
    };
    if (!recv_n_bytes(fd, hdr, 4)) {
        ::close(fd);
        out.message = "train_upload_ack_header_timeout";
        return out;
    }
    uint32_t ack_size = (uint32_t)hdr[0] << 24 | (uint32_t)hdr[1] << 16 |
                        (uint32_t)hdr[2] << 8  | (uint32_t)hdr[3];
    if (ack_size == 0 || ack_size > 4096) {
        ::close(fd);
        out.message = "train_upload_ack_invalid_size";
        return out;
    }
    std::string ack_json(ack_size, '\0');
    if (!recv_n_bytes(fd, ack_json.data(), ack_size)) {
        ::close(fd);
        out.message = "train_upload_ack_body_timeout";
        return out;
    }
    ::close(fd);

    // ACK 파싱 (경량) — success 와 saved_path 추출
    auto find_val = [&](const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\"";
        auto pos = ack_json.find(needle);
        if (pos == std::string::npos) return "";
        auto colon = ack_json.find(':', pos);
        if (colon == std::string::npos) return "";
        // 문자열이면 따옴표 사이, bool 이면 raw
        auto fq = ack_json.find('"', colon);
        auto comma = ack_json.find_first_of(",}", colon);
        if (fq != std::string::npos && fq < comma) {
            auto lq = ack_json.find('"', fq + 1);
            if (lq == std::string::npos) return "";
            return ack_json.substr(fq + 1, lq - fq - 1);
        }
        // non-string value (bool/number)
        std::string raw = ack_json.substr(colon + 1, comma - colon - 1);
        // trim
        while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.erase(raw.begin());
        while (!raw.empty() && (raw.back()  == ' ' || raw.back()  == '\t')) raw.pop_back();
        return raw;
    };

    const std::string success_raw = find_val("success");
    out.success    = (success_raw == "true" || success_raw == "1");
    out.saved_path = find_val("saved_path");
    if (!out.success) out.message = find_val("message");

    return out;
}

} // namespace factory
