#pragma once
// ============================================================================
// log_service.h — /focus, /posture 로그 저장 비즈니스 로직
// ============================================================================
// 명세 (메인서버 요구사항 분석서 §10, §11):
//   /focus   → focus_logs INSERT
//   /posture → posture_logs INSERT
//
// 현재 단건/배치 둘 다 받을 수 있게 컨트롤러에서 분기. 서비스는 단건 INSERT
// 만 노출 — 컨트롤러가 루프 돌리면 됨 (DAO 가 prepared stmt 라 비용 낮음).
// ============================================================================

#include "storage/dao.h"

namespace factory {

class LogService {
public:
    explicit LogService(ConnectionPool& pool)
        : pool_(pool), focus_dao_(pool), posture_dao_(pool),
          event_dao_(pool), session_dao_(pool) {}

    /// session 이 user_id 소유인지 확인. 컨트롤러에서 호출하여 무단 INSERT 차단.
    bool owns_session(long long user_id, long long session_id) {
        if (user_id <= 0 || session_id <= 0) return false;
        auto info = session_dao_.find_by_id(session_id);
        return info.found && info.user_id == user_id;
    }

    // ── 단건 INSERT (트랜잭션 외) — /focus, /posture 호환용 ──
    long long insert_focus  (const FocusLogDao::Entry& e)     { return focus_dao_.insert(e); }
    long long insert_posture(const PostureLogDao::Entry& e)   { return posture_dao_.insert(e); }
    long long insert_event  (const PostureEventDao::Entry& e) { return event_dao_.insert(e); }

    // ── 배치 트랜잭션 INSERT (/log/ingest 용) ──
    struct Batch {
        std::vector<FocusLogDao::Entry>     focuses;
        std::vector<PostureLogDao::Entry>   postures;
        std::vector<PostureEventDao::Entry> events;
    };
    struct BatchResult {
        bool committed       = false;
        int  focus_inserted  = 0;
        int  posture_inserted = 0;
        int  event_inserted  = 0;
        int  failed_lines    = 0;   // 한 행이라도 -1 반환되면 카운트
        std::string error_message;
    };

    /// 단일 커넥션 + autocommit OFF 로 묶어서 한 번에 처리.
    /// 어느 한 INSERT 라도 -1 이면 ROLLBACK 후 모든 변경 무효.
    /// 멱등 EVENT INSERT 에서 기존 row 반환(>0) 은 성공으로 간주.
    BatchResult ingest_transactional(const Batch& batch);

private:
    ConnectionPool& pool_;
    FocusLogDao     focus_dao_;
    PostureLogDao   posture_dao_;
    PostureEventDao event_dao_;
    SessionDao      session_dao_;
};

} // namespace factory
