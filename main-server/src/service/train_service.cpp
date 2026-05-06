// ============================================================================
// train_service.cpp — 학습 완료(TRAIN_COMPLETE) 처리 서비스
// ============================================================================
// 책임:
//   TrainHandler 가 호출하는 단일 진입점 process() 를 제공하여
//   (1) 페이로드 검증 → (2) 모델 파일 atomic 저장 → (3) models 테이블 INSERT
//   순서로 실행. 각 단계 실패 시 이전 단계 결과를 롤백한다.
//
// 실패 시 롤백 전략:
//   1. validate 실패            → 그대로 실패 반환 (파일/DB 모두 손대지 않음)
//   2. save_model_file 실패     → 파일 없음 + DB INSERT 안 함
//   3. model_dao_.insert 실패   → 이미 저장된 모델 파일 삭제 후 실패 반환
//   → "DB 에는 있는데 파일이 없다" 또는 "파일은 있는데 DB 에 없다" 같은
//     부정합 상태를 남기지 않도록 보장.
//
// atomic 파일 저장:
//   save_model_file() 은 임시파일(.tmp.{pid}.{ns}) 에 먼저 기록 → 크기 검증 →
//   std::filesystem::rename (atomic) 으로 최종 경로로 교체.
//   네트워크 순단으로 모델 바이너리가 끊겨도 "부분 파일" 이 추론서버로
//   전파되는 일이 없도록 함. PID+ns 로 임시파일명을 고유화하여 동시 학습
//   충돌도 방지.
//
// 보안:
//   validate() 에서 version 문자열의 "/", "\\", ".." 를 차단 → path traversal 방지.
//   model 바이너리 500MB 상한 — config.limits.model_max_bytes 와 일치.
// ============================================================================
#include "service/train_service.h"
#include "core/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unistd.h>

namespace factory {

TrainService::TrainService(ConnectionPool& pool)
    : model_dao_(pool) {
}

TrainResult TrainService::process(const TrainCompleteEvent& ev) {
    TrainResult result;

    // 1. 검증
    if (!validate(ev, result.error_message)) {
        log_err_train("검증 실패 | %s", result.error_message.c_str());
        return result;
    }

    // 2. 모델 파일 저장
    if (!ev.model_bytes.empty()) {
        result.saved_model_path = save_model_file(ev);
        if (result.saved_model_path.empty()) {
            result.error_message = "model_file_save_failed";
            return result;
        }
    }

    // 3. DB INSERT (저장된 로컬 경로 사용)
    TrainCompleteEvent ev_copy = ev;
    if (!result.saved_model_path.empty()) {
        ev_copy.model_path = result.saved_model_path;
    }

    if (!model_dao_.insert(ev_copy)) {
        // DB 실패 → 파일 롤백 (삭제)
        if (!result.saved_model_path.empty()) {
            std::filesystem::remove(result.saved_model_path);
            log_train("DB 실패 → 모델 파일 롤백 삭제 | %s", result.saved_model_path.c_str());
        }
        result.error_message = "db_insert_failed";
        result.saved_model_path.clear();
        return result;
    }

    result.success = true;
    log_train("학습 처리 완료 | 모델=%s 버전=%s 경로=%s",
              ev.model_type.c_str(), ev.version.c_str(), result.saved_model_path.c_str());
    return result;
}

bool TrainService::validate(const TrainCompleteEvent& ev, std::string& out_error) {
    if (ev.request_id.empty()) {
        out_error = "empty_request_id";
        return false;
    }
    if (ev.station_id < 1 || ev.station_id > 2) {
        out_error = "invalid_station_id";
        return false;
    }
    if (ev.version.empty() || ev.version.size() > 64) {
        out_error = "invalid_version";
        return false;
    }
    if (ev.model_type.empty() || ev.model_type.size() > 32) {
        out_error = "invalid_model_type";
        return false;
    }
    if (ev.accuracy < 0.0 || ev.accuracy > 1.0) {
        out_error = "invalid_accuracy";
        return false;
    }
    // 모델 바이너리 크기 제한: 최대 500MB
    constexpr std::size_t MAX_MODEL_SIZE = 500ULL * 1024 * 1024;
    if (ev.model_bytes.size() > MAX_MODEL_SIZE) {
        out_error = "model_too_large";
        return false;
    }
    // 버전 문자열에 경로 탐색 문자 차단 (path traversal 방지)
    if (ev.version.find('/') != std::string::npos ||
        ev.version.find('\\') != std::string::npos ||
        ev.version.find("..") != std::string::npos) {
        out_error = "invalid_version_chars";
        return false;
    }
    return true;
}

std::string TrainService::save_model_file(const TrainCompleteEvent& ev) {
    // 원본 확장자 추출
    std::string ext = ".bin";
    auto dot_pos = ev.model_path.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = ev.model_path.substr(dot_pos);
    }

    std::string dir = "./storage/models/station" + std::to_string(ev.station_id);
    std::filesystem::create_directories(dir);

    // 임시파일 → 최종파일 rename 패턴 (atomic, race condition 방지)
    // 임시파일명에 PID + 나노초 타임스탬프로 고유성 보장 — 동시 학습 충돌 방지
    std::string final_path = dir + "/" + ev.version + ext;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string tmp_path = final_path + ".tmp." + std::to_string(::getpid()) +
                           "." + std::to_string(ns);

    std::ofstream ofs(tmp_path, std::ios::binary);
    if (!ofs) {
        log_err_train("모델 임시파일 저장 실패 | %s", tmp_path.c_str());
        return "";
    }

    ofs.write(reinterpret_cast<const char*>(ev.model_bytes.data()),
              static_cast<std::streamsize>(ev.model_bytes.size()));
    ofs.flush();
    if (!ofs.good()) {
        log_err_train("모델 쓰기 실패 | %s", tmp_path.c_str());
        ofs.close();
        std::filesystem::remove(tmp_path);
        return "";
    }
    ofs.close();

    // 임시파일 크기 검증
    auto file_size = std::filesystem::file_size(tmp_path);
    if (file_size != ev.model_bytes.size()) {
        log_err_train("모델 크기 불일치 | 예상=%zu 실제=%zu", ev.model_bytes.size(), file_size);
        std::filesystem::remove(tmp_path);
        return "";
    }

    // atomic rename — 부분 쓰기된 파일이 추론서버에 전달되는 race 차단
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        log_err_train("모델 rename 실패 | %s → %s | %s",
                      tmp_path.c_str(), final_path.c_str(), ec.message().c_str());
        std::filesystem::remove(tmp_path);
        return "";
    }

    log_train("모델 파일 저장 완료 | %s (%zu bytes)", final_path.c_str(), ev.model_bytes.size());
    return final_path;
}

} // namespace factory
