#include "pch.h"
#include "network/TokenStore.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <shlobj.h>

bool TokenStore::save(const std::string& token) const
{
    std::ofstream out(file_path(), std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << token;
    return out.good();
}

std::string TokenStore::load() const
{
    std::ifstream in(file_path());
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void TokenStore::clear() const
{
    std::remove(file_path().c_str());
}

std::string TokenStore::file_path() const
{
    // %APPDATA%/StudySync/token.dat
    char path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::string dir = std::string(path) + "\\StudySync";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\token.dat";
    }
    return "studysync_token.dat";
}
