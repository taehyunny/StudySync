// ============================================================================
// input_validator.h — 입력 검증 유틸
// ============================================================================
// 목적:
//   외부에서 들어오는 문자열/숫자의 형식을 검증한다.
//   DAO 쿼리 전, Service 로직 전에 호출되어 비정상 입력을 걸러낸다.
//
// 기준값 근거:
//   - inspection_id 128자: "stationN-YYYYMMDDHHMMSSmmm-NNNNNN" 형식이므로 충분
//   - username 64자: 일반 운영자 아이디 최대 길이
//   - date YYYY-MM-DD: MariaDB DATE 형식 표준
//   - station_id 1~99: 확장 여지를 남긴 합리적 상한
// ============================================================================
#pragma once

#include <regex>
#include <string>

namespace factory::security {

/// 날짜 문자열 YYYY-MM-DD 형식 검증
/// @return 형식이 맞으면 true
inline bool is_valid_date(const std::string& d) {
    static const std::regex date_re(R"(\d{4}-\d{2}-\d{2})");
    return std::regex_match(d, date_re);
}

/// inspection_id 형식 검증
/// 허용: 영숫자 + `-` + `_` + `.`  (경로 탐색/injection 방지)
inline bool is_safe_inspection_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) ||
              c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

/// username 형식 검증 — 빈 값/지나친 길이 차단
inline bool is_valid_username(const std::string& u) {
    return !u.empty() && u.size() <= 64;
}

/// 모델 version 문자열 검증 (path traversal 방지)
inline bool is_safe_version(const std::string& v) {
    if (v.empty() || v.size() > 64) return false;
    if (v.find('/') != std::string::npos) return false;
    if (v.find('\\') != std::string::npos) return false;
    if (v.find("..") != std::string::npos) return false;
    return true;
}

/// station_id 범위 검증 (0: 전체 필터용, 1~99: 실제 스테이션)
inline bool is_valid_station_filter(int id) {
    return id >= 0 && id <= 99;
}

/// 실제 스테이션 ID 검증 (엄격: 1 또는 2만 허용)
inline bool is_valid_station_id(int id) {
    return id == 1 || id == 2;
}

} // namespace factory::security
