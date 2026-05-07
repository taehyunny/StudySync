#pragma once
// ============================================================================
// log_ingest_controller.h — POST /log/ingest (NDJSON 통합 인입)
// ============================================================================
// 클라 [JsonlBatchUploader] 가 30 line 단위로 NDJSON 묶음 송신.
// 각 line 의 "kind" 필드로 분기:
//   kind = "analysis" → focus_logs + posture_logs 1행씩 INSERT
//   kind = "event"    → posture_events 1행 INSERT (event_id 멱등)
//
// 본문 형식 (Content-Type: application/x-ndjson 또는 application/json):
//   {"kind":"analysis","session_id":1,"timestamp_ms":...,"focus_score":85,...}\n
//   {"kind":"event","session_id":1,"event_id":"...","event_type":"drowsy",...}\n
//
// 반환: { "code":200, "accepted": {"analysis":N,"event":N}, "skipped":N }
// ============================================================================

#include "http/http_server.h"
#include "http/jwt_middleware.h"
#include "service/log_service.h"

namespace factory::http {

class LogIngestController {
public:
    LogIngestController(HttpServer& server, JwtMiddleware& jwt, LogService& service);
    void register_routes();

private:
    HttpServer&    server_;
    JwtMiddleware& jwt_;
    LogService&    service_;
};

} // namespace factory::http
