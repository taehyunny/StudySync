// ============================================================================
// gui_service.h — GUI 클라이언트 요청 처리 서비스
// ============================================================================
// 책임: DAO를 통해 DB 조회/저장을 수행하고 결과를 반환한다.
// JSON 응답 생성이나 TCP 전송은 하지 않는다 (GuiRouter가 담당).
// ============================================================================
#pragma once

#include "storage/dao.h"
#include "storage/password_hash.h"

#include <mutex>
#include <string>
#include <vector>

namespace factory {

// ── 응답 구조체들 ──

struct LoginResult {
    bool success = false;
    std::string username;
    std::string role;
    std::string employee_id;
};

struct RegisterResult {
    bool success = false;
    std::string message;
};

struct RetrainResult {
    bool success = false;
    std::string message;
};

// v0.13.0: 학습용 이미지 1장 업로드 결과 (클라 → 메인 → 학습서버 중계)
struct RetrainUploadResult {
    bool        success = false;
    std::string saved_path;   // 학습서버가 보고한 저장 경로
    std::string message;
};

// v0.14.0: 검사 pause/resume 중계 결과
struct InspectControlResult {
    bool        success = false;    // 모든 대상 추론서버에 성공적으로 중계됨
    int         applied_count = 0;  // 실제 명령이 전달된 연결 수
    std::string message;            // 오류 시 메시지
};

class GuiService {
public:
    /// @param pool        DB 커넥션 풀
    /// @param train_host  학습서버 IP (의존성 주입 — 환경별 변경 가능)
    /// @param train_port  학습서버 포트
    GuiService(ConnectionPool& pool,
               const std::string& train_host,
               uint16_t train_port);

    // 인증
    LoginResult login(const std::string& username, const std::string& password);
    RegisterResult register_user(const std::string& employee_id,
                                  const std::string& username,
                                  const std::string& password,
                                  const std::string& role);

    // 조회
    std::vector<StatsDao::InspectionRecord> get_history(
        int station_filter, const std::string& from,
        const std::string& to, int limit);

    // 단건 조회 — 이력 이미지 on-demand 로드용
    StatsDao::InspectionRecord get_inspection_by_id(int id) {
        return stats_dao_.get_by_id(id);
    }

    StatsDao::StatsResult get_stats(
        int station_filter, const std::string& from, const std::string& to);

    std::vector<ModelDao::ModelInfo> get_models();

    // 재학습 요청 → 학습서버 TCP 중계
    // v0.13.0: session_id 가 비어있지 않으면 클라 업로드 세션 경로를
    //   data_path 로 학습서버에 전달 → 학습서버가 해당 폴더를 학습 소스로 사용.
    RetrainResult request_retrain(int station_id, const std::string& model_type,
                                   const std::string& product_name, int image_count,
                                   const std::string& request_id,
                                   const std::string& session_id = "");

    /// v0.14.0: 검사 pause/resume 명령을 추론서버 연결들에 브로드캐스트.
    ///   station_filter: 0 = 모든 추론서버, 1 = Station1 만, 2 = Station2 만
    ///   action: "pause" | "resume"
    ///   ConnectionRegistry 에 등록된 ai_inference_* 연결의 fd 로 직접 송신
    ///   (request_retrain 처럼 별도 TCP 연결을 맺지 않음 — 이미 연결되어 있음).
    InspectControlResult inspect_control(int station_filter,
                                          const std::string& action,
                                          const std::string& request_id);

    /// v0.13.0: 클라가 올린 학습용 이미지 1장을 로컬에 저장 + 학습서버로 중계.
    /// 저장 경로: ./storage/training_upload/{session_id}/{filename}
    /// 학습서버 전달: TCP 로 TRAIN_DATA_UPLOAD(1108) 송신 후 ACK 대기.
    RetrainUploadResult receive_retrain_upload(
        const std::string& session_id,
        int station_id,
        const std::string& model_type,
        const std::string& filename,
        const std::vector<uint8_t>& image_bytes);

    /// 학습 완료 시 플래그 해제 (TrainHandler에서 호출)
    void set_training_done() {
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
    }

private:
    UserDao  user_dao_;
    ModelDao model_dao_;
    StatsDao stats_dao_;

    // 학습서버 주소 (생성자에서 주입)
    std::string train_host_;
    uint16_t    train_port_;

    // 동시 학습 요청 방지
    std::mutex train_mutex_;
    bool       is_training_ = false;
};

} // namespace factory
