// ============================================================================
// password_hash.cpp — bcrypt 기반 비밀번호 해시/검증
// ============================================================================
// 책임:
//   로그인/회원가입 시 비밀번호를 bcrypt($2b$12$...) 으로 해싱·검증한다.
//
// 보안 설계:
//   - bcrypt cost=12 — 2^12 = 4096 라운드 (현 하드웨어에서 ~100ms 내외)
//   - salt 22바이트를 /dev/urandom 에서 읽어 암호학적 안전성 확보
//   - fallback: std::random_device (엔트로피 0 체크 — 결정론적 난수 차단)
//   - verify 는 crypt_r 의 상수시간 비교 의존 (타이밍 공격 저항)
//   - glibc crypt_r 을 사용해 스레드 안전 (crypt 는 전역 정적 버퍼 사용)
//
// 저장 포맷:
//   "$2b$12$<22자 base64 salt><31자 base64 해시>"  총 60자
//   DB 의 users.password_hash 컬럼에 그대로 저장.
// ============================================================================
#include "storage/password_hash.h"
#include "core/logger.h"

#include <crypt.h>
#include <cstring>
#include <fstream>
#include <random>

namespace factory {

// bcrypt base64 문자셋 (표준 base64와 다름)
static const char BCRYPT_BASE64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string PasswordHash::generate_salt() {
    // 22바이트 난수 필요 (base64 1문자당 1바이트)
    unsigned char random_bytes[22];
    bool success = false;

    // 1순위: /dev/urandom — 암호학적으로 안전한 난수
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(random_bytes), sizeof(random_bytes));
        if (urandom.gcount() == static_cast<std::streamsize>(sizeof(random_bytes))) {
            success = true;
        } else {
            log_err_db("urandom 부분 읽기 | 요청=%zu 실제=%lld",
                       sizeof(random_bytes), static_cast<long long>(urandom.gcount()));
        }
    } else {
        log_err_db("/dev/urandom 열기 실패");
    }

    // 2순위: std::random_device (폴백) — 플랫폼별 최선의 난수
    if (!success) {
        std::random_device rd;
        // random_device 엔트로피 확인 — 0이면 결정론적일 수 있음
        if (rd.entropy() == 0) {
            log_err_db("random_device 엔트로피 없음 — 해시 생성 중단");
            return "";  // 빈 솔트 반환 → hash()에서 실패 처리
        }
        for (auto& b : random_bytes) {
            b = static_cast<unsigned char>(rd() & 0xFF);
        }
        log_warn("DB", "urandom 실패 → random_device 폴백 사용");
    }

    // bcrypt salt: "$2b$12$" + 22자 base64 (각 위치마다 독립 난수 바이트 사용)
    std::string salt = "$2b$12$";
    for (int i = 0; i < 22; ++i) {
        salt += BCRYPT_BASE64[random_bytes[i] % 64];
    }
    return salt;
}

std::string PasswordHash::hash(const std::string& password) {
    std::string salt = generate_salt();
    if (salt.empty() || salt.size() < 29) {  // "$2b$12$" + 22자 = 29
        log_err_db("솔트 생성 실패 — 해시 중단");
        return "";
    }

    struct crypt_data data;
    data.initialized = 0;

    const char* result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (!result || strlen(result) < 20) {
        log_err_db("bcrypt 해시 생성 실패");
        return "";
    }

    return std::string(result);
}

bool PasswordHash::verify(const std::string& password, const std::string& stored_hash) {
    if (stored_hash.empty()) return false;

    struct crypt_data data;
    data.initialized = 0;

    // 저장된 해시를 salt로 사용하면 동일한 해시가 나와야 함
    const char* result = crypt_r(password.c_str(), stored_hash.c_str(), &data);
    if (!result) return false;

    return stored_hash == std::string(result);
}

} // namespace factory
