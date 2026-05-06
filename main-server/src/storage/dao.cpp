// ============================================================================
// dao.cpp — StudySync DAO 구현
// ============================================================================
// 책임:
//   StudySync DB 8개 테이블에 대한 SQL 래퍼. Prepared Statement 만 사용.
//   각 메서드는 성공/실패만 반환하고, 실패 사유는 log_err_db 로 기록.
//
// 시간 처리:
//   AI 서버는 ISO8601("2026-05-06T14:30:00.123+09:00") 로 보내지만 MySQL DATETIME
//   은 'T' 구분자/타임존을 거부하므로 iso8601_to_mysql 로 정규화한다.
//   timestamp_ms 는 그대로 BIGINT 로 저장 — 정밀 정렬/만료 계산용.
//
// NULL 처리:
//   - 빈 문자열 → NULL 로 바인딩 (defect_type, reason, clip_* 등)
//   - 숫자 NULL 컬럼은 Entry 구조체의 has_* 플래그로 표현
// ============================================================================
#include "storage/dao.h"
#include "storage/password_hash.h"
#include "core/logger.h"

#include <cstring>
#include <sstream>

namespace factory {

// ---------------------------------------------------------------------------
// 시간 정규화: ISO8601 → MySQL DATETIME
//   "2026-05-06T14:30:00.123+09:00" → "2026-05-06 14:30:00"
//   이미 MySQL 형식이거나 19자 미만이면 그대로 통과 (DB 가 거부하면 에러로).
// ---------------------------------------------------------------------------
static std::string iso8601_to_mysql(const std::string& ts) {
    if (ts.size() < 19) return ts;
    std::string out = ts.substr(0, 19);
    if (out.size() >= 11 && out[10] == 'T') out[10] = ' ';
    return out;
}

// ---------------------------------------------------------------------------
// MYSQL_BIND 헬퍼들 — bind 셋업의 보일러플레이트 축소
// 호출자는 length / null 플래그를 자기 스택에 들고 있어야 함 (포인터만 저장됨).
// ---------------------------------------------------------------------------
static void bind_long(MYSQL_BIND& b, int* v) {
    b.buffer_type = MYSQL_TYPE_LONG;
    b.buffer = v;
}

static void bind_longlong(MYSQL_BIND& b, long long* v) {
    b.buffer_type = MYSQL_TYPE_LONGLONG;
    b.buffer = v;
}

static void bind_double(MYSQL_BIND& b, double* v) {
    b.buffer_type = MYSQL_TYPE_DOUBLE;
    b.buffer = v;
}

static void bind_float(MYSQL_BIND& b, float* v) {
    b.buffer_type = MYSQL_TYPE_FLOAT;
    b.buffer = v;
}

// 문자열 바인딩 (NULL 허용). empty 가 true 이면 NULL 로 바인딩.
//  - len: 호출자 스택에 잡혀 있어야 함 (bind 가 포인터를 들고감)
//  - null: 호출자 스택에 잡혀 있어야 함 (1 바이트)
static void bind_string(MYSQL_BIND& b, const std::string& s,
                        unsigned long& len, my_bool& null,
                        bool nullable_when_empty = false) {
    len  = static_cast<unsigned long>(s.size());
    null = (nullable_when_empty && s.empty()) ? 1 : 0;
    b.buffer_type   = MYSQL_TYPE_STRING;
    b.buffer        = const_cast<char*>(s.c_str());
    b.buffer_length = len;
    b.length        = &len;
    if (nullable_when_empty) b.is_null = &null;
}

// ============================================================================
// UserDao — users 테이블
// ============================================================================

long long UserDao::insert(const std::string& email,
                          const std::string& password,
                          const std::string& name) {
    if (email.empty() || email.size() > 255) return -1;
    if (password.empty() || password.size() > 128) return -1;
    if (name.empty() || name.size() > 100) return -1;

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    std::string hashed = PasswordHash::hash(password);
    if (hashed.empty()) {
        log_err_db("UserDao 비밀번호 해시 실패 | email=%s", email.c_str());
        return -1;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO users (email, password_hash, name, created_at) "
        "VALUES (?, ?, ?, NOW())";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("UserDao insert prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[3];
    std::memset(bind, 0, sizeof(bind));
    unsigned long l_email = 0, l_hash = 0, l_name = 0;
    my_bool n_email = 0, n_hash = 0, n_name = 0;
    bind_string(bind[0], email,  l_email, n_email);
    bind_string(bind[1], hashed, l_hash,  n_hash);
    bind_string(bind[2], name,   l_name,  n_name);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("UserDao insert execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT users | id=%lld email=%s name=%s", id, email.c_str(), name.c_str());
    return id;
}

UserDao::UserInfo UserDao::find_by_email(const std::string& email) {
    UserInfo info;
    if (email.empty() || email.size() > 255) return info;

    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return info;

    const char* sql = "SELECT id, email, name, password_hash FROM users WHERE email=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("UserDao find prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    unsigned long l_email = 0; my_bool n_email = 0;
    bind_string(bp[0], email, l_email, n_email);

    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("UserDao find execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND br[4]; std::memset(br, 0, sizeof(br));
    long long r_id = 0;
    char r_email[256]{}, r_name[128]{}, r_hash[256]{};
    unsigned long l_e = 0, l_n = 0, l_h = 0;

    bind_longlong(br[0], &r_id);
    br[1].buffer_type = MYSQL_TYPE_STRING; br[1].buffer = r_email;
    br[1].buffer_length = sizeof(r_email); br[1].length = &l_e;
    br[2].buffer_type = MYSQL_TYPE_STRING; br[2].buffer = r_name;
    br[2].buffer_length = sizeof(r_name);  br[2].length = &l_n;
    br[3].buffer_type = MYSQL_TYPE_STRING; br[3].buffer = r_hash;
    br[3].buffer_length = sizeof(r_hash);  br[3].length = &l_h;

    if (mysql_stmt_bind_result(stmt, br) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    if (mysql_stmt_fetch(stmt) == 0) {
        info.id    = r_id;
        info.email.assign(r_email, l_e);
        info.name.assign(r_name, l_n);
        info.password_hash.assign(r_hash, l_h);
        info.found = true;
    }
    mysql_stmt_close(stmt);
    return info;
}

bool UserDao::exists_by_email(const std::string& email) {
    return find_by_email(email).found;
}

// ============================================================================
// GoalDao — goals 테이블
// ============================================================================

bool GoalDao::upsert(long long user_id,
                     int daily_goal_min,
                     int rest_interval_min,
                     int rest_duration_min) {
    if (user_id <= 0) return false;
    if (daily_goal_min < 0 || rest_interval_min < 0 || rest_duration_min < 0) return false;

    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    // user_id UNIQUE 라 ON DUPLICATE KEY 로 갱신.
    const char* sql =
        "INSERT INTO goals (user_id, daily_goal_min, rest_interval_min, rest_duration_min) "
        "VALUES (?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE "
        " daily_goal_min=VALUES(daily_goal_min), "
        " rest_interval_min=VALUES(rest_interval_min), "
        " rest_duration_min=VALUES(rest_duration_min)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("GoalDao upsert prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4]; std::memset(bind, 0, sizeof(bind));
    bind_longlong(bind[0], &user_id);
    bind_long(bind[1], &daily_goal_min);
    bind_long(bind[2], &rest_interval_min);
    bind_long(bind[3], &rest_duration_min);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("GoalDao upsert execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    log_db("UPSERT goals | user_id=%lld daily=%d rest_iv=%d rest_dur=%d",
           user_id, daily_goal_min, rest_interval_min, rest_duration_min);
    return true;
}

GoalDao::GoalInfo GoalDao::find_by_user(long long user_id) {
    GoalInfo info;
    if (user_id <= 0) return info;

    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return info;

    const char* sql =
        "SELECT daily_goal_min, rest_interval_min, rest_duration_min "
        "FROM goals WHERE user_id=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    bind_longlong(bp[0], &user_id);

    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND br[3]; std::memset(br, 0, sizeof(br));
    int v_daily = 0, v_rest_iv = 0, v_rest_dur = 0;
    bind_long(br[0], &v_daily);
    bind_long(br[1], &v_rest_iv);
    bind_long(br[2], &v_rest_dur);

    mysql_stmt_bind_result(stmt, br);
    if (mysql_stmt_fetch(stmt) == 0) {
        info.daily_goal_min    = v_daily;
        info.rest_interval_min = v_rest_iv;
        info.rest_duration_min = v_rest_dur;
        info.found = true;
    }
    mysql_stmt_close(stmt);
    return info;
}

// ============================================================================
// SessionDao — sessions 테이블
// ============================================================================

long long SessionDao::start(long long user_id,
                            const std::string& start_time,
                            const std::string& date) {
    if (user_id <= 0) return -1;
    if (start_time.empty()) return -1;

    std::string ts_mysql = iso8601_to_mysql(start_time);
    // date 미지정 시 start_time 의 날짜 부분 사용
    std::string date_str = date.empty() ? ts_mysql.substr(0, 10) : date;
    if (date_str.size() != 10) return -1;

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO sessions (user_id, date, start_time) VALUES (?, ?, ?)";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("SessionDao start prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[3]; std::memset(bind, 0, sizeof(bind));
    unsigned long l_date = 0, l_ts = 0; my_bool n_date = 0, n_ts = 0;
    bind_longlong(bind[0], &user_id);
    bind_string(bind[1], date_str, l_date, n_date);
    bind_string(bind[2], ts_mysql, l_ts,   n_ts);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("SessionDao start execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT sessions | id=%lld user_id=%lld date=%s start=%s",
           id, user_id, date_str.c_str(), ts_mysql.c_str());
    return id;
}

bool SessionDao::end(long long session_id,
                     const std::string& end_time,
                     double focus_min,
                     double avg_focus,
                     bool goal_achieved) {
    if (session_id <= 0) return false;
    if (end_time.empty()) return false;

    std::string ts_mysql = iso8601_to_mysql(end_time);

    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char* sql =
        "UPDATE sessions SET end_time=?, focus_min=?, avg_focus=?, goal_achieved=? "
        "WHERE id=?";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("SessionDao end prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[5]; std::memset(bind, 0, sizeof(bind));
    unsigned long l_ts = 0; my_bool n_ts = 0;
    float v_focus_min = static_cast<float>(focus_min);
    float v_avg_focus = static_cast<float>(avg_focus);
    int   v_goal      = goal_achieved ? 1 : 0;

    bind_string(bind[0], ts_mysql, l_ts, n_ts);
    bind_float(bind[1], &v_focus_min);
    bind_float(bind[2], &v_avg_focus);
    bind_long (bind[3], &v_goal);
    bind_longlong(bind[4], &session_id);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("SessionDao end execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    log_db("UPDATE sessions(end) | id=%lld end=%s focus_min=%.1f avg=%.1f goal=%d",
           session_id, ts_mysql.c_str(), focus_min, avg_focus, v_goal);
    return true;
}

long long SessionDao::find_active(long long user_id) {
    if (user_id <= 0) return 0;

    PooledConnection conn(pool_);
    if (!conn.get()) return 0;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return 0;

    // 가장 최근 시작 + 종료 미설정 1건
    const char* sql =
        "SELECT id FROM sessions WHERE user_id=? AND end_time IS NULL "
        "ORDER BY start_time DESC LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    bind_longlong(bp[0], &user_id);
    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND br[1]; std::memset(br, 0, sizeof(br));
    long long sid = 0;
    bind_longlong(br[0], &sid);
    mysql_stmt_bind_result(stmt, br);
    long long out = (mysql_stmt_fetch(stmt) == 0) ? sid : 0;
    mysql_stmt_close(stmt);
    return out;
}

SessionDao::SessionInfo SessionDao::find_by_id(long long session_id) {
    SessionInfo info;
    if (session_id <= 0) return info;

    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return info;

    const char* sql =
        "SELECT id, user_id, date, start_time, end_time, focus_min, avg_focus, goal_achieved "
        "FROM sessions WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    bind_longlong(bp[0], &session_id);
    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND br[8]; std::memset(br, 0, sizeof(br));
    long long r_id = 0, r_uid = 0;
    char r_date[16]{}, r_start[32]{}, r_end[32]{};
    unsigned long l_date = 0, l_start = 0, l_end = 0;
    my_bool null_end = 0;
    float r_focus = 0, r_avg = 0;
    int r_goal = 0;

    bind_longlong(br[0], &r_id);
    bind_longlong(br[1], &r_uid);
    br[2].buffer_type = MYSQL_TYPE_STRING; br[2].buffer = r_date;
    br[2].buffer_length = sizeof(r_date);  br[2].length = &l_date;
    br[3].buffer_type = MYSQL_TYPE_STRING; br[3].buffer = r_start;
    br[3].buffer_length = sizeof(r_start); br[3].length = &l_start;
    br[4].buffer_type = MYSQL_TYPE_STRING; br[4].buffer = r_end;
    br[4].buffer_length = sizeof(r_end);   br[4].length = &l_end;
    br[4].is_null = &null_end;
    bind_float(br[5], &r_focus);
    bind_float(br[6], &r_avg);
    bind_long (br[7], &r_goal);

    mysql_stmt_bind_result(stmt, br);
    if (mysql_stmt_fetch(stmt) == 0) {
        info.id      = r_id;
        info.user_id = r_uid;
        info.date.assign(r_date, l_date);
        info.start_time.assign(r_start, l_start);
        info.end_time = null_end ? "" : std::string(r_end, l_end);
        info.focus_min     = r_focus;
        info.avg_focus     = r_avg;
        info.goal_achieved = (r_goal != 0);
        info.found = true;
    }
    mysql_stmt_close(stmt);
    return info;
}

// ============================================================================
// FocusLogDao — focus_logs 테이블
// ============================================================================

long long FocusLogDao::insert(const Entry& e) {
    if (e.session_id <= 0) return -1;
    if (e.focus_score < 0 || e.focus_score > 100) return -1;
    if (e.ts.empty()) return -1;

    std::string ts_mysql = iso8601_to_mysql(e.ts);

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO focus_logs "
        "(session_id, ts, timestamp_ms, focus_score, state, is_absent, is_drowsy) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("FocusLogDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[7]; std::memset(bind, 0, sizeof(bind));
    long long  v_sid   = e.session_id;
    long long  v_ts_ms = e.timestamp_ms;
    int        v_score = e.focus_score;
    int        v_absent = e.is_absent ? 1 : 0;
    int        v_drowsy = e.is_drowsy ? 1 : 0;
    unsigned long l_ts = 0, l_state = 0; my_bool n_ts = 0, n_state = 0;
    my_bool null_ts_ms = (e.timestamp_ms == 0) ? 1 : 0;

    bind_longlong(bind[0], &v_sid);
    bind_string(bind[1], ts_mysql, l_ts, n_ts);

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer      = &v_ts_ms;
    bind[2].is_null     = &null_ts_ms;

    bind_long(bind[3], &v_score);
    bind_string(bind[4], e.state, l_state, n_state, /*nullable_when_empty=*/true);
    bind_long(bind[5], &v_absent);
    bind_long(bind[6], &v_drowsy);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("FocusLogDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    return id;
}

// ============================================================================
// PostureLogDao — posture_logs 테이블
// ============================================================================

long long PostureLogDao::insert(const Entry& e) {
    if (e.session_id <= 0) return -1;
    if (e.ts.empty()) return -1;

    std::string ts_mysql = iso8601_to_mysql(e.ts);

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO posture_logs "
        "(session_id, ts, timestamp_ms, neck_angle, shoulder_diff, posture_ok, vs_baseline) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("PostureLogDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[7]; std::memset(bind, 0, sizeof(bind));
    long long v_sid   = e.session_id;
    long long v_ts_ms = e.timestamp_ms;
    float v_neck = static_cast<float>(e.neck_angle);
    float v_sh   = static_cast<float>(e.shoulder_diff);
    float v_vsb  = static_cast<float>(e.vs_baseline);
    int   v_ok   = e.posture_ok ? 1 : 0;
    unsigned long l_ts = 0; my_bool n_ts = 0;
    my_bool null_ts_ms = (e.timestamp_ms == 0) ? 1 : 0;
    my_bool null_neck = e.has_neck_angle ? 0 : 1;
    my_bool null_sh   = e.has_shoulder_diff ? 0 : 1;
    my_bool null_vsb  = e.has_vs_baseline ? 0 : 1;

    bind_longlong(bind[0], &v_sid);
    bind_string(bind[1], ts_mysql, l_ts, n_ts);

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = &v_ts_ms; bind[2].is_null = &null_ts_ms;

    bind[3].buffer_type = MYSQL_TYPE_FLOAT;
    bind[3].buffer = &v_neck; bind[3].is_null = &null_neck;

    bind[4].buffer_type = MYSQL_TYPE_FLOAT;
    bind[4].buffer = &v_sh; bind[4].is_null = &null_sh;

    bind_long(bind[5], &v_ok);

    bind[6].buffer_type = MYSQL_TYPE_FLOAT;
    bind[6].buffer = &v_vsb; bind[6].is_null = &null_vsb;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("PostureLogDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    return id;
}

// ============================================================================
// PostureEventDao — posture_events 테이블 (멱등 INSERT)
// ============================================================================

long long PostureEventDao::insert(const Entry& e) {
    if (e.event_id.empty() || e.event_id.size() > 128) return -1;
    if (e.session_id <= 0) return -1;
    if (e.event_type.empty()) return -1;
    if (e.ts.empty()) return -1;

    std::string ts_mysql = iso8601_to_mysql(e.ts);

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    // event_id UNIQUE → 같은 ID 면 ON DUPLICATE KEY 로 NOOP (id=id 갱신).
    // mysql_stmt_insert_id 는 신규 INSERT 면 신규 id, 중복이면 0 반환.
    // 0 이면 별도 SELECT 로 기존 id 조회.
    const char* sql =
        "INSERT INTO posture_events "
        "(event_id, session_id, event_type, severity, reason, ts, timestamp_ms, "
        " clip_id, clip_access, clip_ref, clip_format, frame_count, retention_days, expires_at_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE id=id";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("PostureEventDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[14]; std::memset(bind, 0, sizeof(bind));
    long long v_sid     = e.session_id;
    long long v_ts_ms   = e.timestamp_ms;
    long long v_exp_ms  = e.expires_at_ms;
    int       v_frames  = e.frame_count;
    int       v_retain  = e.retention_days;
    unsigned long l_eid = 0, l_etype = 0, l_sev = 0, l_reason = 0, l_ts = 0;
    unsigned long l_cid = 0, l_caccess = 0, l_cref = 0, l_cfmt = 0;
    my_bool n_eid = 0, n_etype = 0, n_sev = 0, n_reason = 0, n_ts = 0;
    my_bool n_cid = 0, n_caccess = 0, n_cref = 0, n_cfmt = 0;
    my_bool null_exp_ms = e.has_expires_at_ms ? 0 : 1;

    bind_string(bind[0], e.event_id,    l_eid,    n_eid);
    bind_longlong(bind[1], &v_sid);
    bind_string(bind[2], e.event_type,  l_etype,  n_etype);
    bind_string(bind[3], e.severity,    l_sev,    n_sev);
    bind_string(bind[4], e.reason,      l_reason, n_reason, /*nullable_when_empty=*/true);
    bind_string(bind[5], ts_mysql,      l_ts,     n_ts);

    bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[6].buffer = &v_ts_ms;

    bind_string(bind[7],  e.clip_id,     l_cid,     n_cid,     true);
    bind_string(bind[8],  e.clip_access, l_caccess, n_caccess);
    bind_string(bind[9],  e.clip_ref,    l_cref,    n_cref,    true);
    bind_string(bind[10], e.clip_format, l_cfmt,    n_cfmt,    true);

    bind_long(bind[11], &v_frames);
    bind_long(bind[12], &v_retain);

    bind[13].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[13].buffer = &v_exp_ms; bind[13].is_null = &null_exp_ms;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("PostureEventDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    // 신규 INSERT → insert_id > 0. 중복(ON DUPLICATE) → insert_id == 0 → 기존 id 조회.
    if (id == 0) {
        MYSQL_STMT* q = mysql_stmt_init(conn);
        if (!q) return -1;
        const char* sel = "SELECT id FROM posture_events WHERE event_id=? LIMIT 1";
        if (mysql_stmt_prepare(q, sel, std::strlen(sel)) == 0) {
            MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
            unsigned long l = 0; my_bool n = 0;
            bind_string(bp[0], e.event_id, l, n);
            if (mysql_stmt_bind_param(q, bp) == 0 && mysql_stmt_execute(q) == 0) {
                MYSQL_BIND br[1]; std::memset(br, 0, sizeof(br));
                long long existing = 0;
                bind_longlong(br[0], &existing);
                mysql_stmt_bind_result(q, br);
                if (mysql_stmt_fetch(q) == 0) id = existing;
            }
        }
        mysql_stmt_close(q);
        log_db("posture_events 중복 (멱등) | event_id=%s id=%lld", e.event_id.c_str(), id);
    } else {
        log_db("INSERT posture_events | id=%lld event_id=%s type=%s",
               id, e.event_id.c_str(), e.event_type.c_str());
    }
    return id;
}

// ============================================================================
// TrainDataDao — train_data 테이블
// ============================================================================

long long TrainDataDao::insert(long long user_id,
                               const std::string& ts,
                               const std::string& landmarks_json,
                               const std::string& label) {
    if (user_id <= 0) return -1;
    if (ts.empty()) return -1;
    if (landmarks_json.empty()) return -1;
    if (label != "focus" && label != "distracted" && label != "drowsy") return -1;

    std::string ts_mysql = iso8601_to_mysql(ts);

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO train_data (user_id, ts, landmarks_json, label) VALUES (?,?,?,?)";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("TrainDataDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[4]; std::memset(bind, 0, sizeof(bind));
    unsigned long l_ts = 0, l_json = 0, l_label = 0;
    my_bool n_ts = 0, n_json = 0, n_label = 0;
    bind_longlong(bind[0], &user_id);
    bind_string(bind[1], ts_mysql,       l_ts,    n_ts);
    bind_string(bind[2], landmarks_json, l_json,  n_json);
    bind_string(bind[3], label,          l_label, n_label);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("TrainDataDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    return id;
}

long long TrainDataDao::count_unused(long long user_id) {
    if (user_id <= 0) return 0;

    PooledConnection conn(pool_);
    if (!conn.get()) return 0;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return 0;

    const char* sql =
        "SELECT COUNT(*) FROM train_data WHERE user_id=? AND used_for_training=0";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    bind_longlong(bp[0], &user_id);
    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND br[1]; std::memset(br, 0, sizeof(br));
    long long cnt = 0;
    bind_longlong(br[0], &cnt);
    mysql_stmt_bind_result(stmt, br);
    long long out = (mysql_stmt_fetch(stmt) == 0) ? cnt : 0;
    mysql_stmt_close(stmt);
    return out;
}

bool TrainDataDao::mark_used(long long user_id, long long max_id) {
    if (user_id <= 0 || max_id <= 0) return false;

    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char* sql =
        "UPDATE train_data SET used_for_training=1 "
        "WHERE user_id=? AND id<=? AND used_for_training=0";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("TrainDataDao mark prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2]; std::memset(bind, 0, sizeof(bind));
    bind_longlong(bind[0], &user_id);
    bind_longlong(bind[1], &max_id);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("TrainDataDao mark execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    my_ulonglong affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    log_db("UPDATE train_data(used_for_training=1) | user_id=%lld max_id=%lld affected=%llu",
           user_id, max_id, static_cast<unsigned long long>(affected));
    return true;
}

// ============================================================================
// ModelDao — models 테이블 (StudySync 단순화 버전)
// ============================================================================

long long ModelDao::insert(const std::string& model_type,
                           const std::string& version,
                           double accuracy,
                           const std::string& file_path) {
    if (model_type.empty() || model_type.size() > 50) return -1;
    if (version.empty() || version.size() > 50) return -1;

    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    // 같은 model_type 의 기존 활성 모델을 비활성화 (트랜잭션 없이 best-effort).
    {
        MYSQL_STMT* deact = mysql_stmt_init(conn);
        if (deact) {
            const char* upd = "UPDATE models SET is_active=0 WHERE model_type=? AND is_active=1";
            if (mysql_stmt_prepare(deact, upd, std::strlen(upd)) == 0) {
                MYSQL_BIND b[1]; std::memset(b, 0, sizeof(b));
                unsigned long l = 0; my_bool n = 0;
                bind_string(b[0], model_type, l, n);
                if (mysql_stmt_bind_param(deact, b) == 0) {
                    mysql_stmt_execute(deact);
                }
            }
            mysql_stmt_close(deact);
        }
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    // (model_type, version) UNIQUE → 같은 버전 재등록 시 ON DUPLICATE 로 갱신.
    const char* sql =
        "INSERT INTO models (model_type, version, accuracy, file_path, is_active) "
        "VALUES (?, ?, ?, ?, 1) "
        "ON DUPLICATE KEY UPDATE accuracy=VALUES(accuracy), file_path=VALUES(file_path), is_active=1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("ModelDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[4]; std::memset(bind, 0, sizeof(bind));
    unsigned long l_mt = 0, l_ver = 0, l_path = 0;
    my_bool n_mt = 0, n_ver = 0, n_path = 0;
    float v_acc = static_cast<float>(accuracy);

    bind_string(bind[0], model_type, l_mt,  n_mt);
    bind_string(bind[1], version,    l_ver, n_ver);
    bind_float(bind[2], &v_acc);
    bind_string(bind[3], file_path,  l_path, n_path, /*nullable_when_empty=*/true);

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("ModelDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    // ON DUPLICATE 갱신 시 insert_id == 0 → 기존 id 조회
    if (id == 0) {
        ModelInfo existing = get_active(model_type);
        if (existing.id > 0 && existing.version == version) id = existing.id;
    }

    log_db("UPSERT models | id=%lld type=%s version=%s acc=%.4f",
           id, model_type.c_str(), version.c_str(), accuracy);
    return id;
}

std::vector<ModelDao::ModelInfo> ModelDao::list_all() {
    std::vector<ModelInfo> result;
    PooledConnection conn(pool_);
    if (!conn.get()) return result;

    const char* sql =
        "SELECT id, model_type, version, accuracy, file_path, is_active, created_at "
        "FROM models ORDER BY id DESC";
    if (mysql_query(conn, sql) != 0) return result;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ModelInfo m;
        m.id          = row[0] ? std::strtoll(row[0], nullptr, 10) : 0;
        m.model_type  = row[1] ? row[1] : "";
        m.version     = row[2] ? row[2] : "";
        m.accuracy    = row[3] ? std::atof(row[3]) : 0.0;
        m.file_path   = row[4] ? row[4] : "";
        m.is_active   = row[5] ? (std::atoi(row[5]) != 0) : false;
        m.created_at  = row[6] ? row[6] : "";
        result.push_back(m);
    }
    mysql_free_result(res);
    return result;
}

ModelDao::ModelInfo ModelDao::get_active(const std::string& model_type) {
    ModelInfo info;
    if (model_type.empty()) return info;

    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return info;

    const char* sql =
        "SELECT id, model_type, version, accuracy, file_path, is_active, created_at "
        "FROM models WHERE model_type=? AND is_active=1 ORDER BY id DESC LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND bp[1]; std::memset(bp, 0, sizeof(bp));
    unsigned long l = 0; my_bool n = 0;
    bind_string(bp[0], model_type, l, n);
    if (mysql_stmt_bind_param(stmt, bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND br[7]; std::memset(br, 0, sizeof(br));
    long long r_id = 0;
    char r_mt[64]{}, r_ver[64]{}, r_path[512]{}, r_created[32]{};
    unsigned long l_mt = 0, l_ver = 0, l_path = 0, l_created = 0;
    my_bool null_path = 0;
    float r_acc = 0;
    int r_active = 0;

    bind_longlong(br[0], &r_id);
    br[1].buffer_type = MYSQL_TYPE_STRING; br[1].buffer = r_mt;
    br[1].buffer_length = sizeof(r_mt);    br[1].length = &l_mt;
    br[2].buffer_type = MYSQL_TYPE_STRING; br[2].buffer = r_ver;
    br[2].buffer_length = sizeof(r_ver);   br[2].length = &l_ver;
    bind_float(br[3], &r_acc);
    br[4].buffer_type = MYSQL_TYPE_STRING; br[4].buffer = r_path;
    br[4].buffer_length = sizeof(r_path);  br[4].length = &l_path;
    br[4].is_null = &null_path;
    bind_long (br[5], &r_active);
    br[6].buffer_type = MYSQL_TYPE_STRING; br[6].buffer = r_created;
    br[6].buffer_length = sizeof(r_created); br[6].length = &l_created;

    mysql_stmt_bind_result(stmt, br);
    if (mysql_stmt_fetch(stmt) == 0) {
        info.id         = r_id;
        info.model_type.assign(r_mt, l_mt);
        info.version.assign(r_ver, l_ver);
        info.accuracy   = r_acc;
        info.file_path  = null_path ? "" : std::string(r_path, l_path);
        info.is_active  = (r_active != 0);
        info.created_at.assign(r_created, l_created);
    }
    mysql_stmt_close(stmt);
    return info;
}

} // namespace factory
