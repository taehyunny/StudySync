// ============================================================================
// log_service.cpp — 배치 트랜잭션 인입
// ============================================================================
// /log/ingest 가 30 line 단위로 들어오는데, 중간 한 행이 INSERT 실패해도
// 앞 행들이 그대로 박히면 데이터 부분 파편. 모두 한 트랜잭션으로 묶어
// 부분 실패 시 ROLLBACK 으로 일관성 보장.
//
// 구현:
//   1) PooledConnection 1개 획득
//   2) mysql_autocommit(conn, 0) — 트랜잭션 시작
//   3) 각 Entry 를 DAO::insert_in_conn(conn, ...) 으로 INSERT
//   4) 어느 한 결과가 -1 이면 mysql_rollback() + autocommit(1) 복원 → 실패 반환
//   5) 모두 성공이면 mysql_commit() + autocommit(1) 복원 → 성공 반환
//
// 주의:
//   - PostureEventDao 는 멱등 INSERT (ON DUPLICATE KEY) — 중복 event_id 도
//     성공(>0) 으로 처리되므로 ROLLBACK 트리거 X.
//   - 컨트롤러가 ownership 검증 후 Batch 채워서 넘김. 여기서는 검증 안 함.
// ============================================================================
#include "service/log_service.h"
#include "core/logger.h"

#include <mariadb/mysql.h>

namespace factory {

LogService::BatchResult LogService::ingest_transactional(const Batch& batch) {
    BatchResult r;

    // 빈 배치는 NOOP
    if (batch.focuses.empty() && batch.postures.empty() && batch.events.empty()) {
        r.committed = true;
        return r;
    }

    PooledConnection pc(pool_);
    MYSQL* conn = pc.get();
    if (!conn) {
        r.error_message = "db_acquire_failed";
        log_err_db("/log/ingest 트랜잭션: 커넥션 획득 실패");
        return r;
    }

    // autocommit OFF
    if (mysql_autocommit(conn, 0) != 0) {
        r.error_message = "autocommit_off_failed";
        log_err_db("/log/ingest 트랜잭션: autocommit OFF 실패 | %s", mysql_error(conn));
        return r;
    }

    bool failed = false;

    for (const auto& e : batch.focuses) {
        if (failed) break;
        long long id = focus_dao_.insert_in_conn(conn, e);
        if (id > 0)      ++r.focus_inserted;
        else if (id < 0) { ++r.failed_lines; failed = true; }
    }
    for (const auto& e : batch.postures) {
        if (failed) break;
        long long id = posture_dao_.insert_in_conn(conn, e);
        if (id > 0)      ++r.posture_inserted;
        else if (id < 0) { ++r.failed_lines; failed = true; }
    }
    for (const auto& e : batch.events) {
        if (failed) break;
        long long id = event_dao_.insert_in_conn(conn, e);
        // 멱등: 중복 event_id 도 기존 id (>0) 반환되어 성공으로 카운트
        if (id > 0)      ++r.event_inserted;
        else if (id < 0) { ++r.failed_lines; failed = true; }
    }

    if (failed) {
        if (mysql_rollback(conn) != 0) {
            log_err_db("/log/ingest ROLLBACK 실패 | %s", mysql_error(conn));
        }
        r.error_message = "insert_failed_rollback";
        log_err_db("/log/ingest 부분 실패 → ROLLBACK | failed_lines=%d", r.failed_lines);
    } else {
        if (mysql_commit(conn) != 0) {
            r.error_message = "commit_failed";
            log_err_db("/log/ingest COMMIT 실패 | %s", mysql_error(conn));
            mysql_rollback(conn);
        } else {
            r.committed = true;
        }
    }

    // autocommit 복원 (다음 사용자에게 깨끗한 conn 반납)
    mysql_autocommit(conn, 1);

    if (r.committed) {
        log_db("/log/ingest COMMIT | focus=%d posture=%d event=%d",
               r.focus_inserted, r.posture_inserted, r.event_inserted);
    }
    return r;
}

} // namespace factory
