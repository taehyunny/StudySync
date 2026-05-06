// ============================================================================
// train_service.h — 학습 완료 처리 서비스
// ============================================================================
// 목적:
//   모델 파일 저장 → DB INSERT를 트랜잭션으로 묶는다.
//   실패 시 파일을 삭제하고 에러를 반환한다.
//
// 호출자: TrainHandler
// 사용 DAO: ModelDao
// ============================================================================
#pragma once

#include "storage/dao.h"
#include "core/event_types.h"

#include <string>

namespace factory {

struct TrainResult {
    bool        success = false;
    std::string saved_model_path;
    std::string error_message;
};

class TrainService {
public:
    explicit TrainService(ConnectionPool& pool);

    // 검증 + 모델 파일 저장 + DB INSERT
    TrainResult process(const TrainCompleteEvent& ev);

private:
    bool validate(const TrainCompleteEvent& ev, std::string& out_error);
    std::string save_model_file(const TrainCompleteEvent& ev);

    ModelDao model_dao_;
};

} // namespace factory
