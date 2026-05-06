#pragma once
// ============================================================================
// train_service.h — 학습 완료(TRAIN_COMPLETE) 처리 서비스
// ============================================================================
// 검증 → 모델 파일 atomic 저장 → models 테이블 INSERT 를 묶는다.
// 실패 시 직전 단계까지를 롤백.
//
// StudySync 변경:
//   - station_id 의존 제거 (스키마에 컬럼 없음). model_type 기반 식별.
//   - 저장 디렉토리: ./storage/models/{model_type}/{version}.{ext}
// ============================================================================

#include "storage/dao.h"
#include "core/event_types.h"

#include <string>

namespace factory {

struct TrainResult {
    bool        success = false;
    long long   row_id  = 0;            // models 테이블 row id
    std::string saved_model_path;
    std::string error_message;
};

class TrainService {
public:
    explicit TrainService(ConnectionPool& pool);

    /// 검증 + 파일 저장 + DB INSERT.
    TrainResult process(const TrainCompleteEvent& ev);

private:
    bool validate(const TrainCompleteEvent& ev, std::string& out_error);
    std::string save_model_file(const TrainCompleteEvent& ev);

    ModelDao model_dao_;
};

} // namespace factory
