#pragma once

#include <string>

// POST /feedback — 사용자가 "틀렸어요"를 클릭했을 때
// multipart/form-data 형식으로 이벤트 메타데이터 + 클립 파일을 서버에 전송한다.
//
// 사용 예:
//   FeedbackRequest req;
//   req.event_id     = event.event_id;
//   req.session_id   = session_id_;
//   req.model_pred   = event.reason;
//   req.user_feedback = "wrong";
//   req.consent_ver  = ConsentStore::kCurrentVersion;
//   req.clip_path    = clip_dir;   // 클립이 없으면 빈 문자열
//   bool ok = FeedbackApi::send(req);

struct FeedbackRequest {
    std::string event_id;       // e.g. "evt-1746514205123"
    long long   session_id = 0; // 세션 ID
    std::string model_pred;     // e.g. "drowsy", "bad_posture"
    std::string user_feedback;  // "wrong" | "correct"
    std::string consent_ver;    // e.g. "v1.0"
    std::string clip_path;      // 클립 파일 경로 (없으면 빈 문자열)
};

struct FeedbackResponse {
    bool saved = false;
    int  status_code = 0;
};

class FeedbackApi {
public:
    // POST /feedback — 성공 시 FeedbackResponse.saved == true
    static FeedbackResponse send(const FeedbackRequest& req);

private:
    static constexpr const char* kEndpoint = "/feedback";
};
