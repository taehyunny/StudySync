// ============================================================================
// dao.h — 테이블별 데이터 접근 객체 (DAO)
// ============================================================================
// 목적:
//   각 테이블(inspections, assemblies, models, users)별로
//   SQL 로직을 분리하여 모듈화한다.
//   ConnectionPool에서 커넥션을 빌려 사용한다.
//
// 사용법:
//   InspectionDao dao(pool);
//   long long id = dao.insert(ev);
// ============================================================================
#pragma once

#include "storage/connection_pool.h"
#include "core/event_types.h"

#include <string>
#include <vector>

namespace factory {

// ── InspectionDao — inspections 테이블 ──
class InspectionDao {
public:
    explicit InspectionDao(ConnectionPool& pool) : pool_(pool) {}

    /// 검사 결과 INSERT. 성공 시 생성된 ID 반환, 실패 시 -1.
    /// @param image_path      원본 JPEG 파일 경로 (빈 문자열이면 DB NULL)
    /// @param heatmap_path    Anomaly Map PNG 파일 경로 (v0.9.0+)
    /// @param pred_mask_path  Pred Mask PNG 파일 경로 (v0.9.0+)
    long long insert(const InspectionEvent& ev,
                     const std::string& image_path = "",
                     const std::string& heatmap_path = "",
                     const std::string& pred_mask_path = "");

private:
    ConnectionPool& pool_;
};

// ── AssemblyDao — assemblies 테이블 (Station2 전용) ──
class AssemblyDao {
public:
    explicit AssemblyDao(ConnectionPool& pool) : pool_(pool) {}

    /// 조립 검사 상세 INSERT. 성공 시 생성된 ID 반환, 실패 시 -1
    long long insert(const InspectionEvent& ev, long long inspection_id);

private:
    ConnectionPool& pool_;
    static int extract_int(const std::string& json, const std::string& key);
    static double extract_double(const std::string& json, const std::string& key);
    static std::string extract_array(const std::string& json, const std::string& key);
};

// ── ModelDao — models 테이블 ──
class ModelDao {
public:
    explicit ModelDao(ConnectionPool& pool) : pool_(pool) {}

    /// 학습 완료 모델 INSERT. 성공 시 true
    bool insert(const TrainCompleteEvent& ev);

    /// 모델 목록 조회 (JSON 배열 문자열로 반환)
    struct ModelInfo {
        int id;
        int station_id;
        std::string model_type;
        std::string version;
        double accuracy;
        std::string file_path;      // v0.15.1: 모델 바이너리 파일 경로
        std::string deployed_at;
        int is_active;
    };
    std::vector<ModelInfo> list_all();

private:
    ConnectionPool& pool_;
};

// ── UserDao — users 테이블 ──
class UserDao {
public:
    explicit UserDao(ConnectionPool& pool) : pool_(pool) {}

    struct UserInfo {
        std::string employee_id;
        std::string role;
        std::string password_hash;
        bool found = false;
    };

    /// username으로 사용자 조회
    UserInfo find_by_username(const std::string& username);

    /// 회원가입 INSERT. 성공 시 true
    bool insert(const std::string& employee_id, const std::string& username,
                const std::string& password, const std::string& role);

    /// username 중복 확인
    bool exists(const std::string& username);

    /// 마지막 로그인 시각 갱신
    void update_last_login(const std::string& username);

private:
    ConnectionPool& pool_;
};

// ── StatsDao — inspections 테이블 집계 조회 ──
class StatsDao {
public:
    explicit StatsDao(ConnectionPool& pool) : pool_(pool) {}

    struct StatsResult {
        int total = 0, ok_count = 0, ng_count = 0;
        int s1_ok = 0, s1_ng = 0, s2_ok = 0, s2_ng = 0;
        double avg_latency = 0.0;
        double ng_rate = 0.0;
    };

    StatsResult get_stats(int station_filter, const std::string& date_from,
                          const std::string& date_to);

    struct InspectionRecord {
        int id, station_id, latency_ms;
        std::string timestamp, result, defect_type;
        std::string image_path;       // 원본 JPEG 경로
        std::string heatmap_path;     // Anomaly Map PNG 경로 (v0.9.0+)
        std::string pred_mask_path;   // Pred Mask PNG 경로 (v0.9.0+)
        double confidence;
    };

    std::vector<InspectionRecord> get_history(int station_filter,
                                               const std::string& date_from,
                                               const std::string& date_to,
                                               int limit);

    // 단건 조회 — 이미지 on-demand 로드용
    // 반환: 해당 id의 레코드(한 건). 없으면 id==0.
    InspectionRecord get_by_id(int id);

private:
    ConnectionPool& pool_;
};

} // namespace factory
