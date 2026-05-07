#pragma once

#include <string>

class TokenStore {
public:
    bool save(const std::string& token) const;
    std::string load() const;
    void clear() const;

private:
    std::string file_path() const;
};
