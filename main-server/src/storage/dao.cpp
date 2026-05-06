// ============================================================================
// dao.cpp — 테이블별 DAO 구현 (Data Access Object)
// ============================================================================
// 책임:
//   MariaDB 테이블 4개에 대한 SQL 래퍼:
//     InspectionDao  → inspections       (모든 검사 결과)
//     AssemblyDao    → assemblies        (Station2 조립 상세)
//     ModelDao       → models            (배포된 모델 이력)
//     UserDao        → users             (로그인 계정)
//     StatsDao       → inspections (조회 + 집계)
//
// 공통 설계:
//   - Prepared Statement 사용 → SQL Injection 원천 차단
//   - ConnectionPool 에서 PooledConnection(RAII) 으로 커넥션 획득.
//     스코프 벗어나면 자동으로 풀에 반납 → 누수 방지.
//   - 실패 시 -1/false/빈 객체 반환 + log_err_db 기록.
//
// 타임스탬프 처리:
//   AiServer 가 ISO8601 ("2026-04-20T10:36:31.916+00:00") 로 보내지만
//   MySQL DATETIME 은 공백 구분자를 요구하므로 iso8601_to_mysql 로 변환.
//
// 주의 사항:
//   - 일부 컬럼(bottle_id, model_id) 은 NULL 허용 — AI 시스템이 해당 필드를
//     보내지 않아도 삽입 가능하도록 의도적으로 느슨하게 설정.
//   - list_all 등 대량 조회는 LIMIT 없이 전체 반환 — 대상 테이블이
//     작을 때만 안전 (models, users 등).
// ============================================================================
#include "storage/dao.h"
#include "storage/password_hash.h"
#include "security/input_validator.h"
#include "core/logger.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace factory {

// ============================================================================
// InspectionDao — inspections 테이블 (모든 검사 결과 저장)
// ---------------------------------------------------------------------------
// 스키마 (요약):
//   id              PK AUTO_INCREMENT
//   inspection_id   VARCHAR — 추론서버 발급 유니크 ID
//   station_id      TINYINT (1=입고, 2=조립)
//   result          ENUM('OK','NG')
//   defect_type     VARCHAR NULL
//   confidence      FLOAT
//   image_path      VARCHAR NULL (원본 JPEG)
//   heatmap_path    VARCHAR NULL (Anomaly Heatmap)
//   pred_mask_path  VARCHAR NULL (Pred Mask)
//   model_id        INT NULL — 현재 AI가 미전송으로 항상 NULL (DB_README 참조)
//   bottle_id       INT NULL
//   latency_ms      INT
//   created_at      DATETIME
// ============================================================================

// ISO8601 → MySQL DATETIME 변환
//   입력 예시: "2026-04-20T10:36:31.916+00:00"  (ISO8601 with milliseconds + timezone)
//   출력 예시: "2026-04-20 10:36:31"            (MySQL DATETIME 표준)
//
// 변환 이유: MariaDB의 DATETIME 컬럼은 ISO8601의 'T' 구분자와 timezone 표기를
//           직접 받지 못해 "Incorrect datetime value" 에러 발생.
//           파싱하여 표준 포맷으로 변환 필요.
static std::string iso8601_to_mysql(const std::string& ts) {
    // 최소 길이 체크: "YYYY-MM-DDTHH:MM:SS"는 19자
    //                 이보다 짧으면 비정상 입력 → 그대로 반환 (DB 레벨에서 에러 처리)
    if (ts.size() < 19) return ts;

    // 앞 19자만 잘라냄: 밀리초와 timezone 무시
    //   "2026-04-20T10:36:31.916+00:00" → "2026-04-20T10:36:31"
    std::string out = ts.substr(0, 19);

    // ISO의 'T'(위치 10)를 공백으로 교체 → MySQL 표준
    //   "2026-04-20T10:36:31" → "2026-04-20 10:36:31"
    // 안전성 체크: size() >= 11 조건으로 범위 밖 접근 방지
    if (out.size() >= 11 && out[10] == 'T') out[10] = ' ';

    return out;
}

long long InspectionDao::insert(const InspectionEvent& ev,
                                 const std::string& image_path,
                                 const std::string& heatmap_path,
                                 const std::string& pred_mask_path) {
    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    // ISO8601을 MySQL DATETIME 형식으로 변환
    std::string ts_mysql = iso8601_to_mysql(ev.timestamp);

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) { log_err_db("InspectionDao stmt_init 실패"); return -1; }

    // v0.9.0+ : heatmap_path / pred_mask_path 컬럼 추가 (총 9개 바인딩)
    const char* sql =
        "INSERT INTO inspections "
        "(station_id, timestamp, result, confidence, defect_type, "
        " image_path, heatmap_path, pred_mask_path, latency_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("InspectionDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[9];
    std::memset(bind, 0, sizeof(bind));

    int p_station_id = ev.station_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &p_station_id;

    unsigned long ts_len = static_cast<unsigned long>(ts_mysql.size());
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(ts_mysql.c_str());
    bind[1].buffer_length = ts_len;
    bind[1].length = &ts_len;

    unsigned long result_len = static_cast<unsigned long>(ev.result.size());
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(ev.result.c_str());
    bind[2].buffer_length = result_len;
    bind[2].length = &result_len;

    float p_confidence = static_cast<float>(ev.score);
    bind[3].buffer_type = MYSQL_TYPE_FLOAT;
    bind[3].buffer = &p_confidence;

    unsigned long defect_len = static_cast<unsigned long>(ev.defect_type.size());
    my_bool defect_null = ev.defect_type.empty() ? 1 : 0;
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(ev.defect_type.c_str());
    bind[4].buffer_length = defect_len;
    bind[4].length = &defect_len;
    bind[4].is_null = &defect_null;

    // 경로 3종 — 빈 문자열이면 NULL로 바인딩
    unsigned long img_len = static_cast<unsigned long>(image_path.size());
    my_bool img_null = image_path.empty() ? 1 : 0;
    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = const_cast<char*>(image_path.c_str());
    bind[5].buffer_length = img_len;
    bind[5].length = &img_len;
    bind[5].is_null = &img_null;

    unsigned long hmap_len = static_cast<unsigned long>(heatmap_path.size());
    my_bool hmap_null = heatmap_path.empty() ? 1 : 0;
    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer = const_cast<char*>(heatmap_path.c_str());
    bind[6].buffer_length = hmap_len;
    bind[6].length = &hmap_len;
    bind[6].is_null = &hmap_null;

    unsigned long mask_len = static_cast<unsigned long>(pred_mask_path.size());
    my_bool mask_null = pred_mask_path.empty() ? 1 : 0;
    bind[7].buffer_type = MYSQL_TYPE_STRING;
    bind[7].buffer = const_cast<char*>(pred_mask_path.c_str());
    bind[7].buffer_length = mask_len;
    bind[7].length = &mask_len;
    bind[7].is_null = &mask_null;

    int p_latency = ev.latency_ms;
    bind[8].buffer_type = MYSQL_TYPE_LONG;
    bind[8].buffer = &p_latency;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_err_db("InspectionDao bind 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_err_db("InspectionDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);

    log_db("INSERT inspections | id=%lld station=%d result=%s", id, ev.station_id, ev.result.c_str());
    return id;
}

// ============================================================================
// AssemblyDao — assemblies 테이블 (Station2 조립검사 전용 상세)
// ---------------------------------------------------------------------------
// inspection_id FK 로 inspections 와 1:1 연결.
// Station2 의 YOLO 결과(캡/라벨/액면) 및 PatchCore 점수를 저장.
//
// 스키마 (요약):
//   inspection_id   FK → inspections.id
//   cap_ok          BOOLEAN (뚜껑 정상?)
//   label_ok        BOOLEAN (라벨 정상?)
//   fill_ok         BOOLEAN (충전량 정상?)
//   patchcore_score FLOAT   (라벨 표면 이상 점수)
//   detections_json TEXT    (YOLO 디텍션 배열 원본 JSON)
//
// 가벼운 JSON 필드 추출 유틸 (extract_int/double/array) 이 클래스 내부에
// 따로 정의되어 있음 — Router 의 것과 기능은 유사하나 "배열 값" 추출(extract_array)
// 이 추가되어 있어 YOLO detections 원문 저장에 사용.
// ============================================================================

int AssemblyDao::extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<int>(std::strtol(json.c_str() + colon + 1, nullptr, 10));
}

double AssemblyDao::extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + colon + 1, nullptr);
}

std::string AssemblyDao::extract_array(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "[]";
    auto bracket = json.find('[', pos);
    if (bracket == std::string::npos) return "[]";
    int depth = 0;
    for (std::size_t i = bracket; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') { depth--; if (depth == 0) return json.substr(bracket, i - bracket + 1); }
    }
    return "[]";
}

long long AssemblyDao::insert(const InspectionEvent& ev, long long inspection_id) {
    PooledConnection conn(pool_);
    if (!conn.get()) return -1;

    int cap_ok = extract_int(ev.raw_json, "cap_ok");
    int label_ok = extract_int(ev.raw_json, "label_ok");
    int fill_ok = extract_int(ev.raw_json, "fill_ok");
    float patchcore_score = static_cast<float>(extract_double(ev.raw_json, "patchcore_score"));
    std::string yolo_detections = extract_array(ev.raw_json, "detections");

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -1;

    const char* sql =
        "INSERT INTO assemblies "
        "(inspection_id, cap_ok, label_ok, fill_ok, yolo_detections, patchcore_score) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("AssemblyDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[6];
    std::memset(bind, 0, sizeof(bind));

    int p_insp_id = static_cast<int>(inspection_id);
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &p_insp_id;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &cap_ok;

    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &label_ok;

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = &fill_ok;

    unsigned long yolo_len = static_cast<unsigned long>(yolo_detections.size());
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(yolo_detections.c_str());
    bind[4].buffer_length = yolo_len;
    bind[4].length = &yolo_len;

    bind[5].buffer_type = MYSQL_TYPE_FLOAT;
    bind[5].buffer = &patchcore_score;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("AssemblyDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT assemblies | id=%lld inspection_id=%lld", id, inspection_id);
    return id;
}

// ============================================================================
// ModelDao — models 테이블 (학습된 모델 이력 + 배포 상태)
// ---------------------------------------------------------------------------
// 스키마 (기획서 v0.12 ERD 기준, 실제 DB 와 일치):
//   id           PK AUTO_INCREMENT
//   station_id   TINYINT (1 or 2)
//   model_type   ENUM('PatchCore','YOLO11')
//   version      VARCHAR(20)  (예: 'v20260422_1530')
//   accuracy     FLOAT        (AUROC 또는 mAP50)
//   file_path    VARCHAR(255) (메인서버 로컬 저장 경로) — 기획서 ERD 컬럼명
//   deployed_at  DATETIME     (NOW())
//   is_active    TINYINT      (1=활성, 0=과거 버전)
//   trained_by   INT NULL FK → users.id
//
// v0.15.1: 컬럼명을 기획서 ERD 기준 `file_path` 로 통일.
//   이전 코드가 `model_path` 로 INSERT 시도 → "Unknown column" 에러로
//   학습 완료 배포 파이프라인이 전부 실패하던 문제 수정.
//   C++ 내부 변수명은 `ev.model_path` 유지(범위 최소화) — SQL 컬럼에만 `file_path`.
//
// insert() 는 학습 완료 시 호출되어 is_active=1 로 삽입.
// 같은 (station_id, model_type) 의 과거 행을 0 으로 내리는 로직은 현재 없음 —
// 필요 시 쿼리 추가 (UPDATE models SET is_active=0 WHERE station_id=? AND model_type=? AND id<>LAST_INSERT_ID()).
// ============================================================================

bool ModelDao::insert(const TrainCompleteEvent& ev) {
    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    // v0.15.1: 컬럼명 `file_path` (기획서 ERD / 실제 DB 와 일치).
    //   C++ 변수는 여전히 ev.model_path — 의미는 동일(모델 파일 경로).
    const char* sql =
        "INSERT INTO models (station_id, model_type, version, accuracy, file_path, deployed_at, is_active) "
        "VALUES (?, ?, ?, ?, ?, NOW(), 1)";

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        log_err_db("ModelDao prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    int station_id = ev.station_id;
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &station_id;

    unsigned long mt_len = static_cast<unsigned long>(ev.model_type.size());
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(ev.model_type.c_str());
    bind[1].buffer_length = mt_len;
    bind[1].length = &mt_len;

    unsigned long ver_len = static_cast<unsigned long>(ev.version.size());
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(ev.version.c_str());
    bind[2].buffer_length = ver_len;
    bind[2].length = &ver_len;

    double accuracy = ev.accuracy;
    bind[3].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[3].buffer = &accuracy;

    unsigned long path_len = static_cast<unsigned long>(ev.model_path.size());
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = const_cast<char*>(ev.model_path.c_str());
    bind[4].buffer_length = path_len;
    bind[4].length = &path_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("ModelDao execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    long long id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    mysql_stmt_close(stmt);
    log_db("INSERT models | id=%lld 스테이션=%d 모델=%s 버전=%s",
           id, ev.station_id, ev.model_type.c_str(), ev.version.c_str());
    return true;
}

std::vector<ModelDao::ModelInfo> ModelDao::list_all() {
    std::vector<ModelInfo> result;
    PooledConnection conn(pool_);
    if (!conn.get()) return result;

    // v0.15.1: file_path 컬럼도 함께 SELECT — 클라이언트 PageStation1 이
    //   "어떤 파일이 배포됐는가" 까지 표시할 수 있도록 확장.
    //   Station1/Station2 + PatchCore/YOLO11 구분은 station_id + model_type 조합.
    const char* sql =
        "SELECT id, station_id, model_type, version, accuracy, file_path, deployed_at, is_active "
        "FROM models ORDER BY id DESC";

    if (mysql_query(conn, sql) != 0) return result;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ModelInfo m;
        m.id = row[0] ? std::atoi(row[0]) : 0;
        m.station_id = row[1] ? std::atoi(row[1]) : 0;
        m.model_type = row[2] ? row[2] : "";
        m.version = row[3] ? row[3] : "";
        m.accuracy = row[4] ? std::atof(row[4]) : 0;
        m.file_path = row[5] ? row[5] : "";            // v0.15.1
        m.deployed_at = row[6] ? row[6] : "";
        m.is_active = row[7] ? std::atoi(row[7]) : 0;
        result.push_back(m);
    }
    mysql_free_result(res);
    return result;
}

// ============================================================================
// UserDao — users 테이블 (로그인 계정)
// ---------------------------------------------------------------------------
// 스키마 (요약):
//   id              PK AUTO_INCREMENT
//   employee_id     VARCHAR UNIQUE
//   username        VARCHAR UNIQUE
//   password_hash   VARCHAR(60)   — bcrypt "$2b$12$..." 60자
//   role            VARCHAR       — 'admin' / 'user' 등
//   created_at      DATETIME
//   last_login_at   DATETIME NULL
//
// 저장 시 평문 비밀번호는 PasswordHash::hash() 로 변환. verify 는 stored_hash 를
// salt 로 재사용하여 crypt_r 재실행 → 결과 비교 (상수시간 비교).
//
// 보안:
//   - username 자체는 case-sensitive 로 비교 → 대/소문자 섞여도 서로 다른 계정.
//   - last_login_at 은 로그인 성공 시마다 UPDATE — 세션 활동 추적용.
// ============================================================================

UserDao::UserInfo UserDao::find_by_username(const std::string& username) {
    UserInfo info;
    if (username.empty() || username.size() > 64) return info;

    PooledConnection conn(pool_);
    if (!conn.get()) return info;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return info;

    const char* sql = "SELECT employee_id, role, password_hash FROM users WHERE username=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("UserDao find prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return info;
    }

    MYSQL_BIND bind_param[1];
    std::memset(bind_param, 0, sizeof(bind_param));
    unsigned long uname_len = static_cast<unsigned long>(username.size());
    bind_param[0].buffer_type = MYSQL_TYPE_STRING;
    bind_param[0].buffer = const_cast<char*>(username.c_str());
    bind_param[0].buffer_length = uname_len;
    bind_param[0].length = &uname_len;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("UserDao find execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return info;
    }

    // 결과 바인딩
    MYSQL_BIND bind_res[3];
    std::memset(bind_res, 0, sizeof(bind_res));
    char buf_emp[128]{}, buf_role[64]{}, buf_hash[256]{};
    unsigned long len_emp = 0, len_role = 0, len_hash = 0;

    bind_res[0].buffer_type = MYSQL_TYPE_STRING;
    bind_res[0].buffer = buf_emp;  bind_res[0].buffer_length = sizeof(buf_emp);
    bind_res[0].length = &len_emp;

    bind_res[1].buffer_type = MYSQL_TYPE_STRING;
    bind_res[1].buffer = buf_role; bind_res[1].buffer_length = sizeof(buf_role);
    bind_res[1].length = &len_role;

    bind_res[2].buffer_type = MYSQL_TYPE_STRING;
    bind_res[2].buffer = buf_hash; bind_res[2].buffer_length = sizeof(buf_hash);
    bind_res[2].length = &len_hash;

    if (mysql_stmt_bind_result(stmt, bind_res) != 0) {
        mysql_stmt_close(stmt);
        return info;
    }

    if (mysql_stmt_fetch(stmt) == 0) {
        info.employee_id.assign(buf_emp, len_emp);
        info.role.assign(buf_role, len_role);
        info.password_hash.assign(buf_hash, len_hash);
        info.found = true;
    }
    mysql_stmt_close(stmt);
    return info;
}

bool UserDao::exists(const std::string& username) {
    if (username.empty() || username.size() > 64) return false;

    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char* sql = "SELECT id FROM users WHERE username=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    unsigned long uname_len = static_cast<unsigned long>(username.size());
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(username.c_str());
    bind[0].buffer_length = uname_len;
    bind[0].length = &uname_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    // 결과 확인
    MYSQL_BIND bind_res[1];
    std::memset(bind_res, 0, sizeof(bind_res));
    int id_val = 0;
    bind_res[0].buffer_type = MYSQL_TYPE_LONG;
    bind_res[0].buffer = &id_val;

    mysql_stmt_bind_result(stmt, bind_res);
    bool found = (mysql_stmt_fetch(stmt) == 0);
    mysql_stmt_close(stmt);
    return found;
}

bool UserDao::insert(const std::string& employee_id, const std::string& username,
                     const std::string& password, const std::string& role) {
    if (username.empty() || username.size() > 64) return false;
    if (employee_id.empty() || employee_id.size() > 32) return false;
    if (password.empty() || password.size() > 128) return false;
    if (role.empty() || role.size() > 16) return false;

    PooledConnection conn(pool_);
    if (!conn.get()) return false;

    // 평문 비밀번호를 bcrypt 해시로 변환
    std::string hashed = PasswordHash::hash(password);
    if (hashed.empty()) {
        log_err_db("비밀번호 해시 생성 실패 | 사용자=%s", username.c_str());
        return false;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;

    const char* sql =
        "INSERT INTO users (employee_id, username, password_hash, role, created_at) "
        "VALUES (?, ?, ?, ?, NOW())";

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        log_err_db("UserDao insert prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[4];
    std::memset(bind, 0, sizeof(bind));

    unsigned long emp_len = static_cast<unsigned long>(employee_id.size());
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(employee_id.c_str());
    bind[0].buffer_length = emp_len;
    bind[0].length = &emp_len;

    unsigned long uname_len = static_cast<unsigned long>(username.size());
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(username.c_str());
    bind[1].buffer_length = uname_len;
    bind[1].length = &uname_len;

    unsigned long hash_len = static_cast<unsigned long>(hashed.size());
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = const_cast<char*>(hashed.c_str());
    bind[2].buffer_length = hash_len;
    bind[2].length = &hash_len;

    unsigned long role_len = static_cast<unsigned long>(role.size());
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = const_cast<char*>(role.c_str());
    bind[3].buffer_length = role_len;
    bind[3].length = &role_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_err_db("UserDao insert execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    log_db("INSERT users | 사용자=%s 사번=%s", username.c_str(), employee_id.c_str());
    return true;
}

void UserDao::update_last_login(const std::string& username) {
    if (username.empty() || username.size() > 64) return;

    PooledConnection conn(pool_);
    if (!conn.get()) return;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return;

    const char* sql = "UPDATE users SET last_login_at=NOW() WHERE username=?";
    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    unsigned long uname_len = static_cast<unsigned long>(username.size());
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(username.c_str());
    bind[0].buffer_length = uname_len;
    bind[0].length = &uname_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        return;
    }
    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
}

// ============================================================================
// StatsDao — inspections 테이블의 집계/조회 전담
// ---------------------------------------------------------------------------
// 다른 DAO 와 다르게 쓰기(INSERT/UPDATE) 는 없고 **읽기 전용**.
// CPageStats / CPageHome 의 대시보드, INSPECT_HISTORY 검색 등을 지원.
//
// 주요 메서드:
//   get_stats()   — 기간별 OK/NG 집계 + 스테이션별 집계 + 평균 지연시간
//                   (단일 집계 SQL 1회 수행 — 행 단위 루프 없음)
//   get_history() — 기간/스테이션 필터로 개별 검사 row 목록 (LIMIT 적용)
//   get_by_id()   — 이미지 상세보기용 단건 조회 (inspection_id 로 경로 조회)
//
// 입력 검증:
//   is_valid_date (security 모듈) 로 date_from/date_to 포맷 검증 —
//   SQL Injection 은 prepared statement 로 차단되지만 포맷 오류 조기 거부 목적.
// ============================================================================

using factory::security::is_valid_date;

StatsDao::StatsResult StatsDao::get_stats(int station_filter,
                                           const std::string& date_from,
                                           const std::string& date_to) {
    StatsResult r;
    // 날짜 입력 검증
    if (!date_from.empty() && !is_valid_date(date_from)) return r;
    if (!date_to.empty()   && !is_valid_date(date_to))   return r;
    if (station_filter < 0 || station_filter > 99) return r;

    PooledConnection conn(pool_);
    if (!conn.get()) return r;

    // 동적 WHERE절 — 조건별 prepared statement 구성
    std::ostringstream sql;
    sql << "SELECT station_id, result, COUNT(*), AVG(latency_ms) "
        << "FROM inspections WHERE 1=1";
    if (station_filter > 0) sql << " AND station_id=?";
    if (!date_from.empty()) sql << " AND timestamp>=?";
    if (!date_to.empty())   sql << " AND timestamp<=?";
    sql << " GROUP BY station_id, result";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return r;

    std::string sql_str = sql.str();
    if (mysql_stmt_prepare(stmt, sql_str.c_str(), static_cast<unsigned long>(sql_str.size())) != 0) {
        log_err_db("StatsDao stats prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return r;
    }

    MYSQL_BIND bind_params[3];
    std::memset(bind_params, 0, sizeof(bind_params));
    int bind_idx = 0;
    std::string date_to_full = date_to.empty() ? "" : (date_to + " 23:59:59");
    unsigned long len_from = 0, len_to = 0;

    if (station_filter > 0) {
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_LONG;
        bind_params[bind_idx].buffer = &station_filter;
        bind_idx++;
    }
    if (!date_from.empty()) {
        len_from = static_cast<unsigned long>(date_from.size());
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_STRING;
        bind_params[bind_idx].buffer = const_cast<char*>(date_from.c_str());
        bind_params[bind_idx].buffer_length = len_from;
        bind_params[bind_idx].length = &len_from;
        bind_idx++;
    }
    if (!date_to.empty()) {
        len_to = static_cast<unsigned long>(date_to_full.size());
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_STRING;
        bind_params[bind_idx].buffer = const_cast<char*>(date_to_full.c_str());
        bind_params[bind_idx].buffer_length = len_to;
        bind_params[bind_idx].length = &len_to;
        bind_idx++;
    }

    if (bind_idx > 0 && mysql_stmt_bind_param(stmt, bind_params) != 0) {
        mysql_stmt_close(stmt);
        return r;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_err_db("StatsDao stats execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return r;
    }

    // 결과 바인딩
    MYSQL_BIND bind_res[4];
    std::memset(bind_res, 0, sizeof(bind_res));
    int res_sid = 0; char res_result[16]{}; unsigned long res_result_len = 0;
    long long res_cnt = 0; double res_lat = 0;

    bind_res[0].buffer_type = MYSQL_TYPE_LONG;
    bind_res[0].buffer = &res_sid;
    bind_res[1].buffer_type = MYSQL_TYPE_STRING;
    bind_res[1].buffer = res_result; bind_res[1].buffer_length = sizeof(res_result);
    bind_res[1].length = &res_result_len;
    bind_res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_res[2].buffer = &res_cnt;
    bind_res[3].buffer_type = MYSQL_TYPE_DOUBLE;
    bind_res[3].buffer = &res_lat;

    mysql_stmt_bind_result(stmt, bind_res);

    double lat_sum = 0; int lat_cnt = 0;
    while (mysql_stmt_fetch(stmt) == 0) {
        int cnt = static_cast<int>(res_cnt);
        std::string result_str(res_result, res_result_len);

        r.total += cnt;
        lat_sum += res_lat * cnt; lat_cnt += cnt;

        if (result_str == "ok") {
            r.ok_count += cnt;
            if (res_sid == 1) r.s1_ok += cnt; else r.s2_ok += cnt;
        } else {
            r.ng_count += cnt;
            if (res_sid == 1) r.s1_ng += cnt; else r.s2_ng += cnt;
        }
    }
    if (lat_cnt > 0) r.avg_latency = lat_sum / lat_cnt;
    r.ng_rate = r.total > 0 ? (100.0 * r.ng_count / r.total) : 0.0;
    mysql_stmt_close(stmt);
    return r;
}

std::vector<StatsDao::InspectionRecord> StatsDao::get_history(
    int station_filter, const std::string& date_from,
    const std::string& date_to, int limit) {

    std::vector<InspectionRecord> records;
    // 입력 검증
    if (!date_from.empty() && !is_valid_date(date_from)) return records;
    if (!date_to.empty()   && !is_valid_date(date_to))   return records;
    if (station_filter < 0 || station_filter > 99) return records;
    if (limit <= 0 || limit > 500) limit = 100;

    PooledConnection conn(pool_);
    if (!conn.get()) return records;

    std::ostringstream sql;
    sql << "SELECT id, station_id, timestamp, result, confidence, "
        << "defect_type, image_path, heatmap_path, pred_mask_path, latency_ms "
        << "FROM inspections WHERE 1=1";
    if (station_filter > 0) sql << " AND station_id=?";
    if (!date_from.empty()) sql << " AND timestamp>=?";
    if (!date_to.empty())   sql << " AND timestamp<=?";
    sql << " ORDER BY id DESC LIMIT ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return records;

    std::string sql_str = sql.str();
    if (mysql_stmt_prepare(stmt, sql_str.c_str(), static_cast<unsigned long>(sql_str.size())) != 0) {
        log_err_db("StatsDao history prepare 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return records;
    }

    MYSQL_BIND bind_params[4];
    std::memset(bind_params, 0, sizeof(bind_params));
    int bind_idx = 0;
    std::string date_to_full = date_to.empty() ? "" : (date_to + " 23:59:59");
    unsigned long len_from = 0, len_to = 0;

    if (station_filter > 0) {
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_LONG;
        bind_params[bind_idx].buffer = &station_filter;
        bind_idx++;
    }
    if (!date_from.empty()) {
        len_from = static_cast<unsigned long>(date_from.size());
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_STRING;
        bind_params[bind_idx].buffer = const_cast<char*>(date_from.c_str());
        bind_params[bind_idx].buffer_length = len_from;
        bind_params[bind_idx].length = &len_from;
        bind_idx++;
    }
    if (!date_to.empty()) {
        len_to = static_cast<unsigned long>(date_to_full.size());
        bind_params[bind_idx].buffer_type = MYSQL_TYPE_STRING;
        bind_params[bind_idx].buffer = const_cast<char*>(date_to_full.c_str());
        bind_params[bind_idx].buffer_length = len_to;
        bind_params[bind_idx].length = &len_to;
        bind_idx++;
    }
    // LIMIT 파라미터
    bind_params[bind_idx].buffer_type = MYSQL_TYPE_LONG;
    bind_params[bind_idx].buffer = &limit;
    bind_idx++;

    if (mysql_stmt_bind_param(stmt, bind_params) != 0) {
        mysql_stmt_close(stmt);
        return records;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_err_db("StatsDao history execute 실패 | %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return records;
    }

    // 결과 바인딩 — 10개 컬럼 (v0.9.0+: heatmap_path, pred_mask_path 추가)
    MYSQL_BIND bind_res[10];
    std::memset(bind_res, 0, sizeof(bind_res));
    int r_id = 0, r_sid = 0, r_latency = 0;
    char r_ts[64]{}, r_result[16]{}, r_defect[64]{};
    char r_imgpath[256]{}, r_heatpath[256]{}, r_maskpath[256]{};
    double r_conf = 0;
    unsigned long len_ts = 0, len_result = 0, len_defect = 0;
    unsigned long len_imgpath = 0, len_heatpath = 0, len_maskpath = 0;
    my_bool null_defect = 0, null_imgpath = 0, null_heatpath = 0, null_maskpath = 0;

    bind_res[0].buffer_type = MYSQL_TYPE_LONG;    bind_res[0].buffer = &r_id;
    bind_res[1].buffer_type = MYSQL_TYPE_LONG;    bind_res[1].buffer = &r_sid;
    bind_res[2].buffer_type = MYSQL_TYPE_STRING;  bind_res[2].buffer = r_ts;
    bind_res[2].buffer_length = sizeof(r_ts);     bind_res[2].length = &len_ts;
    bind_res[3].buffer_type = MYSQL_TYPE_STRING;  bind_res[3].buffer = r_result;
    bind_res[3].buffer_length = sizeof(r_result); bind_res[3].length = &len_result;
    bind_res[4].buffer_type = MYSQL_TYPE_DOUBLE;  bind_res[4].buffer = &r_conf;
    bind_res[5].buffer_type = MYSQL_TYPE_STRING;  bind_res[5].buffer = r_defect;
    bind_res[5].buffer_length = sizeof(r_defect); bind_res[5].length = &len_defect;
    bind_res[5].is_null = &null_defect;
    bind_res[6].buffer_type = MYSQL_TYPE_STRING;  bind_res[6].buffer = r_imgpath;
    bind_res[6].buffer_length = sizeof(r_imgpath); bind_res[6].length = &len_imgpath;
    bind_res[6].is_null = &null_imgpath;
    bind_res[7].buffer_type = MYSQL_TYPE_STRING;  bind_res[7].buffer = r_heatpath;
    bind_res[7].buffer_length = sizeof(r_heatpath); bind_res[7].length = &len_heatpath;
    bind_res[7].is_null = &null_heatpath;
    bind_res[8].buffer_type = MYSQL_TYPE_STRING;  bind_res[8].buffer = r_maskpath;
    bind_res[8].buffer_length = sizeof(r_maskpath); bind_res[8].length = &len_maskpath;
    bind_res[8].is_null = &null_maskpath;
    bind_res[9].buffer_type = MYSQL_TYPE_LONG;    bind_res[9].buffer = &r_latency;

    mysql_stmt_bind_result(stmt, bind_res);

    while (mysql_stmt_fetch(stmt) == 0) {
        InspectionRecord rec;
        rec.id = r_id;
        rec.station_id = r_sid;
        rec.timestamp.assign(r_ts, len_ts);
        rec.result.assign(r_result, len_result);
        rec.confidence = r_conf;
        rec.defect_type    = null_defect   ? "" : std::string(r_defect,   len_defect);
        rec.image_path     = null_imgpath  ? "" : std::string(r_imgpath,  len_imgpath);
        rec.heatmap_path   = null_heatpath ? "" : std::string(r_heatpath, len_heatpath);
        rec.pred_mask_path = null_maskpath ? "" : std::string(r_maskpath, len_maskpath);
        rec.latency_ms = r_latency;
        records.push_back(rec);
    }
    mysql_stmt_close(stmt);
    return records;
}

// 단건 조회 — 이미지 on-demand 로드 시 이력 항목의 경로만 조회.
// inspection_id는 클라가 HISTORY_RES로 받은 id를 그대로 전달하므로 유효성은 DB에서 보장.
StatsDao::InspectionRecord StatsDao::get_by_id(int id) {
    InspectionRecord empty{};
    if (id <= 0) return empty;

    PooledConnection conn(pool_);
    if (!conn.get()) return empty;

    const char* sql = "SELECT id, station_id, timestamp, result, confidence, "
                      "defect_type, image_path, heatmap_path, pred_mask_path, "
                      "latency_ms FROM inspections WHERE id=? LIMIT 1";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return empty;
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return empty;
    }

    MYSQL_BIND bp{};
    bp.buffer_type = MYSQL_TYPE_LONG;
    bp.buffer = &id;
    if (mysql_stmt_bind_param(stmt, &bp) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return empty;
    }

    MYSQL_BIND bind_res[10];
    std::memset(bind_res, 0, sizeof(bind_res));
    int r_id = 0, r_sid = 0, r_latency = 0;
    char r_ts[64]{}, r_result[16]{}, r_defect[64]{};
    char r_imgpath[256]{}, r_heatpath[256]{}, r_maskpath[256]{};
    double r_conf = 0;
    unsigned long len_ts = 0, len_result = 0, len_defect = 0;
    unsigned long len_imgpath = 0, len_heatpath = 0, len_maskpath = 0;
    my_bool null_defect = 0, null_imgpath = 0, null_heatpath = 0, null_maskpath = 0;

    bind_res[0].buffer_type = MYSQL_TYPE_LONG;    bind_res[0].buffer = &r_id;
    bind_res[1].buffer_type = MYSQL_TYPE_LONG;    bind_res[1].buffer = &r_sid;
    bind_res[2].buffer_type = MYSQL_TYPE_STRING;  bind_res[2].buffer = r_ts;
    bind_res[2].buffer_length = sizeof(r_ts);     bind_res[2].length = &len_ts;
    bind_res[3].buffer_type = MYSQL_TYPE_STRING;  bind_res[3].buffer = r_result;
    bind_res[3].buffer_length = sizeof(r_result); bind_res[3].length = &len_result;
    bind_res[4].buffer_type = MYSQL_TYPE_DOUBLE;  bind_res[4].buffer = &r_conf;
    bind_res[5].buffer_type = MYSQL_TYPE_STRING;  bind_res[5].buffer = r_defect;
    bind_res[5].buffer_length = sizeof(r_defect); bind_res[5].length = &len_defect;
    bind_res[5].is_null = &null_defect;
    bind_res[6].buffer_type = MYSQL_TYPE_STRING;  bind_res[6].buffer = r_imgpath;
    bind_res[6].buffer_length = sizeof(r_imgpath); bind_res[6].length = &len_imgpath;
    bind_res[6].is_null = &null_imgpath;
    bind_res[7].buffer_type = MYSQL_TYPE_STRING;  bind_res[7].buffer = r_heatpath;
    bind_res[7].buffer_length = sizeof(r_heatpath); bind_res[7].length = &len_heatpath;
    bind_res[7].is_null = &null_heatpath;
    bind_res[8].buffer_type = MYSQL_TYPE_STRING;  bind_res[8].buffer = r_maskpath;
    bind_res[8].buffer_length = sizeof(r_maskpath); bind_res[8].length = &len_maskpath;
    bind_res[8].is_null = &null_maskpath;
    bind_res[9].buffer_type = MYSQL_TYPE_LONG;    bind_res[9].buffer = &r_latency;

    mysql_stmt_bind_result(stmt, bind_res);

    InspectionRecord rec{};
    if (mysql_stmt_fetch(stmt) == 0) {
        rec.id = r_id;
        rec.station_id = r_sid;
        rec.timestamp.assign(r_ts, len_ts);
        rec.result.assign(r_result, len_result);
        rec.confidence = r_conf;
        rec.defect_type    = null_defect   ? "" : std::string(r_defect,   len_defect);
        rec.image_path     = null_imgpath  ? "" : std::string(r_imgpath,  len_imgpath);
        rec.heatmap_path   = null_heatpath ? "" : std::string(r_heatpath, len_heatpath);
        rec.pred_mask_path = null_maskpath ? "" : std::string(r_maskpath, len_maskpath);
        rec.latency_ms = r_latency;
    }
    mysql_stmt_close(stmt);
    return rec;
}

} // namespace factory
