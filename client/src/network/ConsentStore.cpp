#include "pch.h"
#include "network/ConsentStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ── 저장 경로 ─────────────────────────────────────────────────────
// %APPDATA%\StudySync\consent.dat

std::string ConsentStore::consent_file_path()
{
    // %APPDATA% 환경변수 읽기
    char appdata[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        // 환경변수 없으면 실행 파일 옆에 저장
        return "consent.dat";
    }

    const fs::path dir = fs::path(appdata) / "StudySync";
    return (dir / "consent.dat").string();
}

// ── is_consented ──────────────────────────────────────────────────

bool ConsentStore::is_consented(const std::string& version)
{
    const std::string path = consent_file_path();
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string stored;
    std::getline(f, stored);

    // 앞뒤 공백·개행 제거
    const auto trim = [](std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
            s.pop_back();
        while (!s.empty() && (s.front() == ' '))
            s.erase(s.begin());
    };
    trim(stored);

    return stored == version;
}

// ── record_consent ────────────────────────────────────────────────

bool ConsentStore::record_consent(const std::string& version)
{
    const std::string path = consent_file_path();

    // 디렉터리 생성 (없으면)
    try {
        const fs::path dir = fs::path(path).parent_path();
        if (!dir.empty()) {
            fs::create_directories(dir);
        }
    } catch (...) {
        OutputDebugStringA("[ConsentStore] failed to create directory\n");
        return false;
    }

    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    if (!f.is_open()) {
        OutputDebugStringA("[ConsentStore] failed to open consent.dat for writing\n");
        return false;
    }

    f << version << "\n";
    f.flush();

    std::ostringstream dbg;
    dbg << "[ConsentStore] consent recorded: " << version
        << "  path=" << path << "\n";
    OutputDebugStringA(dbg.str().c_str());

    return f.good();
}

// ── revoke ────────────────────────────────────────────────────────

bool ConsentStore::revoke()
{
    const std::string path = consent_file_path();
    try {
        if (fs::exists(path)) {
            fs::remove(path);
            OutputDebugStringA("[ConsentStore] consent revoked\n");
        }
        return true;
    } catch (...) {
        OutputDebugStringA("[ConsentStore] revoke failed\n");
        return false;
    }
}
