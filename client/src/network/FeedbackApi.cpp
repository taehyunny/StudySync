#include "pch.h"
#include "network/FeedbackApi.h"
#include "network/WinHttpClient.h"

#include <fstream>
#include <sstream>
#include <vector>

// ── 응답 파싱 헬퍼 ───────────────────────────────────────────────────────────
// {"saved":true} 또는 {"saved":false} 에서 saved 값을 추출한다.
static bool parse_saved(const std::string& body)
{
    const std::string key = "\"saved\"";
    auto pos = body.find(key);
    if (pos == std::string::npos) return false;
    pos += key.size();
    while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ')) ++pos;
    return body.compare(pos, 4, "true") == 0;
}

// ── 파일 → 바이너리 읽기 ────────────────────────────────────────────────────
static std::vector<uint8_t> read_file_bytes(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// ── session_id를 문자열로 변환 ───────────────────────────────────────────────
static std::string to_str(long long v)
{
    return std::to_string(v);
}

// ── 파일명만 추출 (경로에서) ─────────────────────────────────────────────────
static std::string basename(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// ── FeedbackApi::send ────────────────────────────────────────────────────────
FeedbackResponse FeedbackApi::send(const FeedbackRequest& req)
{
    FeedbackResponse result;

    std::vector<MultipartField> fields;

    // ── 텍스트 필드 ────────────────────────────────────────────────────────
    fields.push_back({ "event_id",      req.event_id,               "", "", {} });
    fields.push_back({ "session_id",    to_str(req.session_id),     "", "", {} });
    fields.push_back({ "model_pred",    req.model_pred,             "", "", {} });
    fields.push_back({ "user_feedback", req.user_feedback,          "", "", {} });
    fields.push_back({ "consent_ver",   req.consent_ver,            "", "", {} });

    // ── 클립 파일 (선택) ───────────────────────────────────────────────────
    if (!req.clip_path.empty()) {
        const std::vector<uint8_t> clip_bytes = read_file_bytes(req.clip_path);
        if (!clip_bytes.empty()) {
            MultipartField clip_field;
            clip_field.name         = "clip";
            clip_field.filename     = basename(req.clip_path);
            clip_field.content_type = "application/octet-stream";
            clip_field.data         = clip_bytes;
            fields.push_back(std::move(clip_field));
        } else {
            // 파일을 읽지 못했을 때: 빈 clip_path로 처리 (전송은 계속)
            char dbg[256];
            snprintf(dbg, sizeof(dbg),
                "[FeedbackApi] clip_path='%s' 읽기 실패, clip 필드 없이 전송\n",
                req.clip_path.c_str());
            OutputDebugStringA(dbg);
        }
    }

    const HttpResponse resp =
        WinHttpClient::instance().post_multipart(kEndpoint, fields);

    result.status_code = resp.status_code;
    result.saved       = resp.ok() && parse_saved(resp.body);

    // 결과 디버그 출력
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
            "[FeedbackApi] POST %s -> HTTP %d  saved=%s\n",
            kEndpoint, resp.status_code, result.saved ? "true" : "false");
        OutputDebugStringA(dbg);
    }

    return result;
}
