// ============================================================================
// password_hash.h — bcrypt 비밀번호 해시/검증 유틸리티
// ============================================================================
// glibc의 crypt_r()을 사용하여 bcrypt($2b$) 해시를 생성/검증한다.
// 별도 외부 라이브러리 설치 없이 동작한다.
//
// 사용법:
//   std::string hash = PasswordHash::hash("1234");       // 해시 생성
//   bool ok = PasswordHash::verify("1234", hash);         // 검증
// ============================================================================
#pragma once

#include <string>

namespace factory {

class PasswordHash {
public:
    // 평문 비밀번호를 bcrypt 해시로 변환
    // 반환: "$2b$12$..." 형식 해시 문자열, 실패 시 빈 문자열
    static std::string hash(const std::string& password);

    // 평문 비밀번호와 해시 비교
    // 반환: 일치하면 true
    static bool verify(const std::string& password, const std::string& hash);

private:
    // bcrypt용 랜덤 salt 생성 ("$2b$12$" + 22자 base64)
    static std::string generate_salt();
};

} // namespace factory
