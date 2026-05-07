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
        : focus_dao_(pool), posture_dao_(pool), session_dao_(pool) {}

    /// session 이 user_id 소유인지 확인. 컨트롤러에서 호출하여 무단 INSERT 차단.
    bool owns_session(long long user_id, long long session_id) {
        if (user_id <= 0 || session_id <= 0) return false;
        auto info = session_dao_.find_by_id(session_id);
        return info.found && info.user_id == user_id;
    }

    long long insert_focus(const FocusLogDao::Entry& e)     { return focus_dao_.insert(e); }
    long long insert_posture(const PostureLogDao::Entry& e) { return posture_dao_.insert(e); }

private:
    FocusLogDao   focus_dao_;
    PostureLogDao posture_dao_;
    SessionDao    session_dao_;
};

} // namespace factory
