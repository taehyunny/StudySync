// ============================================================================
// inspection_service.cpp — NG 검사 결과 처리 서비스 (v0.9.0+ 3장 이미지)
// ============================================================================
// 책임:
//   AI 추론서버가 보낸 NG(Station1_NG=1000 / Station2_NG=1002) 패킷의
//   후처리 단일 진입점. validate + 이미지 저장 + DB INSERT 를 원자적 흐름으로
//   처리하고 결과를 InspectionResult 로 반환.
//
// 처리 순서:
//   1. validate()           — station_id/result/score/latency/크기 검증
//   2. save_blob() × 3      — 원본/히트맵/마스크 JPEG/PNG 저장 (NG 만, 각기 개별)
//   3. inspection_dao_.insert — inspections 테이블 INSERT (세 경로 포함)
//   4. Station2 면 assembly_dao_.insert — assemblies 테이블 보충 INSERT
//
// 실패 시 롤백 정책 (의도적 관대 처리):
//   - validate 실패 → 즉시 반환 (아무 작업도 하지 않음)
//   - 이미지 저장 일부 실패 → 해당 경로만 비운 채 DB 는 계속 진행
//                         → "결과는 있지만 이미지가 없는" row 가 남을 수 있음
//   - inspections INSERT 실패 → 이미 저장된 이미지는 고아 파일로 남김 (나중에 청소)
//   - assemblies INSERT 실패 → inspections 는 이미 저장됐으므로 로그만
//
// 관대한 롤백 이유:
//   실시간 검사 파이프라인이라 처리 지연 < 데이터 완전성. 소량의 고아 파일은
//   배치 청소 스크립트에서 정리. 반대 방향 "DB 에만 있고 파일 없음" 도 허용 —
//   클라이언트는 이미지가 없으면 size=0 으로 빈 프레임을 받음.
//
// 이미지 저장 경로:
//   {image_root}/station{N}/{YYYYMMDD}/ng_{epoch_ms}_{suffix}.{ext}
//     예) ./storage/station2/20260422/ng_1745310000123_heatmap.png
//   세 이미지가 같은 epoch_ms 를 공유 → 파일명만 봐도 한 검사 결과임을 식별.
//
// 보안:
//   이미지 50MB 상한, 경로 traversal 미사용 (상대경로 조립만).
//   디스크 여유 100MB 미만 시 저장 거부 (DoS/디스크 풀 방어).
// ============================================================================
#include "service/inspection_service.h"
#include "core/logger.h"

#include <cctype>     // std::tolower (result 문자열 정규화)
#include <chrono>
#include <cmath>      // std::isfinite (score NaN/Inf 체크)
#include <filesystem>
#include <fstream>
#include <functional> // std::hash (thread id → size_t)
#include <sstream>
#include <system_error>
#include <thread>     // std::this_thread::get_id (임시파일명 유일성)
#include <unistd.h>   // getpid()

namespace factory {

InspectionService::InspectionService(EventBus& bus,
                                     ConnectionPool& pool,
                                     const std::string& image_root_dir)
    : event_bus_(bus),
      inspection_dao_(pool),
      assembly_dao_(pool),
      image_root_dir_(image_root_dir) {
}

// ---------------------------------------------------------------------------
// register_handlers — EventBus 구독 등록 (main.cpp 에서 1회 호출)
// INSPECTION_VALIDATED 이벤트를 받으면 백그라운드 워커에서 persist 실행.
// ---------------------------------------------------------------------------
void InspectionService::register_handlers() {
    event_bus_.subscribe(EventType::INSPECTION_VALIDATED,
                         [this](const std::any& p) { this->on_validated(p); });
}

// ---------------------------------------------------------------------------
// validate_only — 빠른 검증만 (I/O 無, <1ms)
// StationHandler 가 호출 → true 면 즉시 ACK + INSPECTION_VALIDATED 발행.
// 부수효과: ev.result 를 "OK"/"NG" 대문자 수용해 소문자로 정규화.
// ---------------------------------------------------------------------------
bool InspectionService::validate_only(InspectionEvent& ev, std::string& out_error) {
    return validate(ev, out_error);
}

// ---------------------------------------------------------------------------
// on_validated — INSPECTION_VALIDATED 이벤트 수신 (EventBus 워커 스레드)
// ACK 는 이미 StationHandler 에서 먼저 전송됐으므로, 여기서 실패해도 재전송
// 불가. 실패는 로그에 또렷이 남기고(운영자가 봐야 함) 이후 처리는 포기.
// ---------------------------------------------------------------------------
void InspectionService::on_validated(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    auto result = persist(ev);
    if (!result.success) {
        // Sliced failure: AI 서버에는 이미 ACK(ok) 를 보냈는데 서버쪽 저장 실패.
        // 데이터 유실 가능 → ERROR 레벨로 강하게 남긴다. 모니터링/알림 연동 권장.
        log_err_db("[SLICED-FAILURE] 저장 실패 (ACK 는 이미 송신됨) | "
                   "id=%s station=%d err=%s",
                   ev.inspection_id.c_str(), ev.station_id,
                   result.error_message.c_str());
        return;
    }

    // 성공: 클라이언트 실시간 푸시 (v0.14.7: DB id 함께 전달)
    // MFC 의 NG 이력 리스트는 id 로 중복 방지하므로 모든 푸시가 동일 id=0 이면
    // 리스트가 한 행만 갱신되고 10건이 안 채워진다. DB row id 를 주입해서
    // 각 NG 가 고유 id 를 가지도록 한다.
    InspectionEvent ev_with_id = ev;
    ev_with_id.db_id = result.inspection_id;
    event_bus_.publish(EventType::GUI_PUSH_REQUESTED, ev_with_id);
}

// ---------------------------------------------------------------------------
// persist — 이미지 3장 저장 + DB INSERT inspections (+ assemblies for station2)
// 순서 주의: 이미지 저장 먼저 → 그 경로를 DB INSERT 에 포함.
// ---------------------------------------------------------------------------
InspectionResult InspectionService::persist(const InspectionEvent& ev) {
    InspectionResult result;

    // 1. NG 이미지 3장 저장 (DB INSERT 전에 먼저 저장 — 경로를 DB에 바로 기록하기 위함)
    //    각 저장 실패는 치명적이지 않고 해당 경로만 비움.
    //    v0.13.3: 파일명에 inspection_id 를 포함시켜 병렬 워커 경합을 원천 차단.
    if (!ev.image_bytes.empty()) {
        result.image_path = save_blob(ev.station_id, ev.timestamp, ev.inspection_id,
                                      ev.image_bytes, "original", ".jpg");
        if (result.image_path.empty()) {
            log_warn("AI", "원본 이미지 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }
    if (!ev.heatmap_bytes.empty()) {
        result.heatmap_path = save_blob(ev.station_id, ev.timestamp, ev.inspection_id,
                                        ev.heatmap_bytes, "heatmap", ".png");
        if (result.heatmap_path.empty()) {
            log_warn("AI", "히트맵 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }
    if (!ev.pred_mask_bytes.empty()) {
        result.pred_mask_path = save_blob(ev.station_id, ev.timestamp, ev.inspection_id,
                                          ev.pred_mask_bytes, "mask", ".png");
        if (result.pred_mask_path.empty()) {
            log_warn("AI", "Pred Mask 저장 실패 | id=%s", ev.inspection_id.c_str());
        }
    }

    // 2. inspections 테이블 INSERT (세 이미지 경로 포함)
    long long inspection_id = inspection_dao_.insert(ev,
                                                     result.image_path,
                                                     result.heatmap_path,
                                                     result.pred_mask_path);
    if (inspection_id < 0) {
        result.error_message = "db_insert_failed";
        log_err_db("INSERT inspections 실패 | id=%s", ev.inspection_id.c_str());
        return result;
    }
    result.inspection_id = inspection_id;

    // 3. Station2면 assemblies 테이블 INSERT
    if (ev.station_id == 2) {
        long long assembly_id = assembly_dao_.insert(ev, inspection_id);
        if (assembly_id < 0) {
            log_err_db("INSERT assemblies 실패 | inspection_id=%lld", inspection_id);
            // inspection은 이미 저장됨 — 치명적이지 않으므로 계속 진행
        }
    }

    result.success = true;
    return result;
}

bool InspectionService::validate(InspectionEvent& ev, std::string& out_error) {
    if (ev.station_id < 1 || ev.station_id > 2) {
        out_error = "invalid_station_id";
        return false;
    }
    if (ev.inspection_id.empty() || ev.inspection_id.size() > 128) {
        out_error = "invalid_inspection_id";
        return false;
    }
    // result 는 대소문자 무관하게 수용 (AiServer 는 "OK"/"NG" 대문자 송신).
    //   - 경로상 소문자 "ok"/"ng" 로 정규화해 이후 로직/DB 저장 일관성 확보.
    {
        std::string r = ev.result;
        for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (r != "ok" && r != "ng") {
            out_error = "invalid_result";
            return false;
        }
        ev.result = r;  // 소문자로 정규화 (이후 DB 저장 일관성)
    }

    // score 는 PatchCore 원시 anomaly_score 를 그대로 받을 수 있어 0~1 로 고정할 수 없다.
    //   - 임계값은 추론서버에서 이미 적용되어 result(ok/ng) 에 반영됨.
    //   - 여기서는 NaN/Inf 등 유효하지 않은 숫자만 차단.
    if (!std::isfinite(ev.score)) {
        out_error = "invalid_score";
        return false;
    }
    if (ev.latency_ms < 0 || ev.latency_ms > 60000) {
        out_error = "invalid_latency";
        return false;
    }
    if (ev.defect_type.size() > 64) {
        out_error = "defect_type_too_long";
        return false;
    }
    // 이미지 크기 제한: 최대 50MB
    if (ev.image_bytes.size() > 50ULL * 1024 * 1024) {
        out_error = "image_too_large";
        return false;
    }
    // timestamp 형식 검증 (최소 10자: YYYY-MM-DD)
    if (ev.timestamp.size() < 10) {
        out_error = "invalid_timestamp";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// save_blob — 이미지 바이너리를 디스크에 atomic 저장 (v0.13.3 race-safe)
//
// 파일명:
//   {root}/station{N}/{YYYYMMDD}/{sanitized_inspection_id}_{suffix}.{ext}
//     예) ./storage/station1/20260422/station1-20260422055748159-000025_original.jpg
//
// 경합 방지 (v0.13.3 이전의 epoch_ms 방식 → inspection_id 기반으로 전환):
//   이전: ng_{ms}_{suffix}.{ext} — 병렬 워커 2개가 같은 ms 에 진입하면 동일 파일
//         충돌 → 크기 불일치/삭제 레이스/파일 사라짐 예외 발생
//   현재: inspection_id 는 "station{N}-{YYYYMMDDHHMMSSmmm}-{seq6}" 로 AI 서버가
//         발급하는 원천 유일 키 → 충돌 원천 불가
//
// atomic write 패턴:
//   1) 임시파일 {path}.tmp.{pid}.{tid} 에 먼저 기록 + flush
//   2) 크기 검증 (try/catch 로 filesystem 예외 흡수)
//   3) std::filesystem::rename 으로 최종 경로로 atomic 교체
//   → 다른 프로세스/스레드가 중간 상태 파일을 보지 못함
//
// 로그 레벨:
//   작은 불일치/디스크 부족 등은 log_err_img, 정상은 log_img.
// ---------------------------------------------------------------------------
std::string InspectionService::save_blob(int station_id,
                                         const std::string& timestamp,
                                         const std::string& inspection_id,
                                         const std::vector<uint8_t>& bytes,
                                         const std::string& suffix,
                                         const std::string& ext) {
    if (bytes.empty()) return "";

    // 날짜 디렉터리 (YYYYMMDD) — timestamp 의 "YYYY-MM-DD" 앞 10자 활용
    std::string yyyymmdd;
    if (timestamp.size() >= 10) {
        yyyymmdd = timestamp.substr(0, 4) +
                   timestamp.substr(5, 2) +
                   timestamp.substr(8, 2);
    } else {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream os;
        os << std::put_time(&tm, "%Y%m%d");
        yyyymmdd = os.str();
    }

    // inspection_id sanitize — 파일명에 안전한 문자만 허용 (영숫자/-/_ 만).
    // '/', '\\', '..' 등 경로 탐색/금지문자 차단.
    std::string safe_id;
    safe_id.reserve(inspection_id.size());
    for (char c : inspection_id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            safe_id.push_back(c);
        } else {
            safe_id.push_back('_');
        }
    }
    if (safe_id.empty()) {
        // 만약 inspection_id 가 비어있거나 다 비허용 문자면 fallback 으로 epoch ms 사용.
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        safe_id = "unk_" + std::to_string(ms);
    }

    std::ostringstream path_os;
    path_os << image_root_dir_ << "/station" << station_id
            << "/" << yyyymmdd << "/" << safe_id << "_" << suffix << ext;
    const std::string final_path = path_os.str();

    // 임시파일 경로 — PID + thread id 로 동일 프로세스 내 스레드 간에도 유일
    std::ostringstream tmp_os;
    tmp_os << final_path << ".tmp." << ::getpid() << "."
           << std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::string tmp_path = tmp_os.str();

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(final_path).parent_path(), ec);
    if (ec) {
        log_err_img("디렉터리 생성 실패 | %s | %s",
                    final_path.c_str(), ec.message().c_str());
        return "";
    }

    // 디스크 여유 공간 확인 (최소 100MB)
    try {
        auto space_info = std::filesystem::space(
            std::filesystem::path(final_path).parent_path());
        if (space_info.available < 100ULL * 1024 * 1024) {
            log_err_img("디스크 여유 공간 부족 | 잔여=%zu MB",
                        space_info.available / (1024 * 1024));
            return "";
        }
    } catch (...) {
        // space() 실패는 치명적이지 않음 — 계속 진행
    }

    // ── 1) 임시파일 쓰기 ──
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) {
            log_err_img("임시파일 열기 실패 | %s", tmp_path.c_str());
            return "";
        }
        ofs.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        ofs.flush();
        if (!ofs.good()) {
            log_err_img("임시파일 쓰기 실패 | %s", tmp_path.c_str());
            ofs.close();
            std::filesystem::remove(tmp_path, ec);
            return "";
        }
    } // ofs close() — RAII

    // ── 2) 크기 검증 (filesystem 예외 흡수) ──
    try {
        auto file_size = std::filesystem::file_size(tmp_path);
        if (file_size != bytes.size()) {
            log_err_img("이미지 크기 불일치 | 예상=%zu 실제=%zu",
                        bytes.size(), file_size);
            std::filesystem::remove(tmp_path, ec);
            return "";
        }
    } catch (const std::exception& exc) {
        log_err_img("파일 크기 확인 실패 | %s | %s",
                    tmp_path.c_str(), exc.what());
        std::filesystem::remove(tmp_path, ec);
        return "";
    }

    // ── 3) atomic rename (tmp → final) ──
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        log_err_img("rename 실패 | %s → %s | %s",
                    tmp_path.c_str(), final_path.c_str(), ec.message().c_str());
        std::filesystem::remove(tmp_path, ec);
        return "";
    }

    log_img("이미지 저장 완료 | %s", final_path.c_str());
    return final_path;
}

} // namespace factory
