#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <windows.h>
#include <winhttp.h>

struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool ok() const { return status_code >= 200 && status_code < 300; }
};

class WinHttpClient {
public:
    static WinHttpClient& instance();
    WinHttpClient();
    ~WinHttpClient();

    void set_base_url(const std::string& base_url);
    void set_token(const std::string& jwt);
    void clear_token();

    HttpResponse get(const std::string& path);
    HttpResponse post_json (const std::string& path, const std::string& json_body);
    HttpResponse post_ndjson(const std::string& path, const std::string& ndjson_body);

private:
    WinHttpClient(const WinHttpClient&) = delete;
    WinHttpClient& operator=(const WinHttpClient&) = delete;

    struct UrlParts {
        bool https = false;
        std::wstring host;
        std::uint16_t port = 80;
    };

    UrlParts parse_base_url() const;
    HttpResponse send_request(const wchar_t* verb, const std::string& path,
                              const std::string& body, const wchar_t* content_type);
    static std::wstring to_wide(const std::string& s);
    static std::string to_utf8(const wchar_t* ws, int len);

    std::mutex mtx_;
    HINTERNET session_ = nullptr;
    std::string base_url_;
    std::string token_;
};
