// ============================================================================
// train_service.cpp — 학습 완료 처리
// ============================================================================
// 흐름:
//   1) validate (request_id, version 경로 탐색 차단, 모델 크기 상한 등)
//   2) atomic 모델 파일 저장 (.tmp.{pid}.{ns} → rename)
//   3) ModelDao::insert (UPSERT — 같은 model_type 의 기존 활성 모델은 비활성화)
//   3 실패 시 → 저장된 파일 삭제 (부정합 방지)
//
// 저장 경로:
//   ./storage/models/{model_type 슬러그}/{version}.{ext}
//   model_type 슬러그: '/' '\\' '..' 제거된 형태 (path traversal 방지)
// ============================================================================
#include "service/train_service.h"
#include "core/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unistd.h>

namespace factory {

namespace {

// 디렉토리/파일명에 안전한 슬러그 — '/' '\\' '..' 만 제거.
// 한글/영숫자/언더스코어/하이픈은 통과.
std::string sanitize_segment(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '/' || c == '\\') continue;
        if (c == '.' && i + 1 < s.size() && s[i + 1] == '.') { ++i; continue; }
        out.push_back(c);
    }
    if (out.empty()) out = "default";
    return out;
}

} // namespace

TrainService::TrainService(ConnectionPool& pool)
    : model_dao_(pool) {
}

TrainResult TrainService::process(const TrainCompleteEvent& ev) {
    TrainResult result;

    if (!validate(ev, result.error_message)) {
        log_err_train("검증 실패 | %s", result.error_message.c_str());
        return result;
    }

    if (!ev.model_bytes.empty()) {
        result.saved_model_path = save_model_file(ev);
        if (result.saved_model_path.empty()) {
            result.error_message = "model_file_save_failed";
            return result;
        }
    }

    long long row_id = model_dao_.insert(ev.model_type, ev.version, ev.accuracy,
                                          result.saved_model_path);
    if (row_id <= 0) {
        if (!result.saved_model_path.empty()) {
            std::filesystem::remove(result.saved_model_path);
            log_train("DB 실패 → 모델 파일 롤백 삭제 | %s",
                      result.saved_model_path.c_str());
        }
        result.error_message = "db_insert_failed";
        result.saved_model_path.clear();
        return result;
    }

    result.row_id  = row_id;
    result.success = true;
    log_train("학습 처리 완료 | type=%s ver=%s row=%lld path=%s",
              ev.model_type.c_str(), ev.version.c_str(),
              row_id, result.saved_model_path.c_str());
    return result;
}

bool TrainService::validate(const TrainCompleteEvent& ev, std::string& out_error) {
    if (ev.request_id.empty()) { out_error = "empty_request_id"; return false; }
    if (ev.version.empty() || ev.version.size() > 50) { out_error = "invalid_version"; return false; }
    if (ev.model_type.empty() || ev.model_type.size() > 50) { out_error = "invalid_model_type"; return false; }
    if (ev.accuracy < 0.0 || ev.accuracy > 1.0) { out_error = "invalid_accuracy"; return false; }

    constexpr std::size_t MAX_MODEL_SIZE = 500ULL * 1024 * 1024;  // 500MB
    if (ev.model_bytes.size() > MAX_MODEL_SIZE) {
        out_error = "model_too_large";
        return false;
    }
    if (ev.version.find('/') != std::string::npos ||
        ev.version.find('\\') != std::string::npos ||
        ev.version.find("..") != std::string::npos) {
        out_error = "invalid_version_chars";
        return false;
    }
    return true;
}

std::string TrainService::save_model_file(const TrainCompleteEvent& ev) {
    // 원본 경로에서 확장자 추출 (없으면 .bin)
    std::string ext = ".bin";
    auto dot_pos = ev.model_path.rfind('.');
    if (dot_pos != std::string::npos && dot_pos + 1 < ev.model_path.size()) {
        std::string candidate = ev.model_path.substr(dot_pos);
        // 확장자에 슬래시 들어있으면 무효 (디렉토리 경로 일부)
        if (candidate.find('/') == std::string::npos &&
            candidate.find('\\') == std::string::npos) {
            ext = candidate;
        }
    }

    std::string dir = "./storage/models/" + sanitize_segment(ev.model_type);
    std::filesystem::create_directories(dir);

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

    auto file_size = std::filesystem::file_size(tmp_path);
    if (file_size != ev.model_bytes.size()) {
        log_err_train("모델 크기 불일치 | 예상=%zu 실제=%zu",
                      ev.model_bytes.size(), file_size);
        std::filesystem::remove(tmp_path);
        return "";
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        log_err_train("모델 rename 실패 | %s → %s | %s",
                      tmp_path.c_str(), final_path.c_str(), ec.message().c_str());
        std::filesystem::remove(tmp_path);
        return "";
    }

    log_train("모델 파일 저장 완료 | %s (%zu bytes)",
              final_path.c_str(), ev.model_bytes.size());
    return final_path;
}

} // namespace factory
