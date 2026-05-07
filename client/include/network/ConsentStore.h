#pragma once

#include <string>

// 사용자 피드백 업로드 동의 여부를 로컬 파일에 영속 저장한다.
//
// 저장 경로: %APPDATA%\StudySync\consent.dat
// 파일 내용: 동의한 약관 버전 문자열 한 줄 (예: "v1.0")
//
// 사용 흐름:
//   1. ReviewDlg "틀렸어요" 클릭
//   2. ConsentStore::is_consented("v1.0") == false  → 동의 팝업 표시
//   3. 사용자 동의  → ConsentStore::record_consent("v1.0")
//   4. 이후 클릭   → is_consented() == true  → 팝업 생략
//
// 동의 철회: revoke() 호출 또는 파일 직접 삭제
//   (설정 화면에서 "데이터 제공 동의 철회" 기능 — 추후 구현)

class ConsentStore {
public:
    static constexpr const char* kCurrentVersion = "v1.0";

    // 저장된 동의 버전이 version과 일치하는지 확인
    static bool is_consented(const std::string& version = kCurrentVersion);

    // 동의 버전을 파일에 기록
    static bool record_consent(const std::string& version = kCurrentVersion);

    // 동의 철회 (파일 삭제)
    static bool revoke();

    // 저장 경로 반환 (디버그·테스트용)
    static std::string consent_file_path();
};
