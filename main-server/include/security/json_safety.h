// ============================================================================
// json_safety.h — JSON 문자열 안전 처리 유틸
// ============================================================================
// 목적:
//   JSON 응답 생성 시 사용자 입력/DB 데이터를 안전하게 이스케이프한다.
//   JSON injection 공격과 파싱 오류를 방지한다.
//
// 처리 대상:
//   - 쌍따옴표     -> 백슬래시 + "
//   - 역슬래시     -> 백슬래시 2개
//   - 개행/CR/탭   -> \n / \r / \t
//   - 백스페이스/폼피드 -> \b / \f
//   - 제어문자(0x00~0x1F) -> \uXXXX
//
// 사용 원칙:
//   - 모든 JSON 응답의 문자열 필드에 반드시 적용
//   - 서버 ↔ 클라이언트, 서버 ↔ AI서버 전송 JSON 동일하게 적용
// ============================================================================
#pragma once

#include <string>

namespace factory::security {

/// JSON 문자열 이스케이프 — injection 방지 + 파싱 안전성 보장
/// @param s 원본 문자열 (DB 데이터, 사용자 입력 등)
/// @return JSON에 삽입해도 안전한 이스케이프된 문자열
inline std::string escape_json(const std::string& s) {
    std::string out;             // 결과를 담을 문자열
    out.reserve(s.size() + 16);  // 메모리 미리 할당 (이스케이프로 길이 증가 대비)
                                 // reserve는 capacity만 늘림 (size는 0 유지)

    // 입력 문자를 하나씩 검사하며 JSON 스펙에 맞게 변환
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;  // " → \" (JSON 문자열 종료 문자 이스케이프)
            case '\\': out += "\\\\"; break;  // \ → \\ (이스케이프 문자 자체 이스케이프)
            case '\n': out += "\\n";  break;  // 개행 → 두 문자 시퀀스 \n
            case '\r': out += "\\r";  break;  // 캐리지리턴 → \r
            case '\t': out += "\\t";  break;  // 탭 → \t
            case '\b': out += "\\b";  break;  // 백스페이스 → \b
            case '\f': out += "\\f";  break;  // 폼피드 → \f
            default:
                // 제어문자(0x00 ~ 0x1F)는 \uXXXX 형식으로 이스케이프
                // unsigned char 캐스팅: char는 signed/unsigned 구현 의존적
                //   → 음수 비교 방지 위해 unsigned로 변환
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];              // "\u00XX\0" (6자 + 여유)
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                                              // %04x = 소문자 16진수 4자리 (앞 0 채움)
                    out += buf;
                } else {
                    // UTF-8 멀티바이트(0x80 이상) 포함 일반 문자는 그대로
                    // JSON은 UTF-8을 허용하므로 이스케이프 불필요
                    out += c;
                }
                break;
        }
    }
    return out;
}

} // namespace factory::security
