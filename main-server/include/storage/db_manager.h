// ============================================================================
// db_manager.h — DB 이벤트 핸들러 (ConnectionPool + DAO 사용)
// ============================================================================
// 목적:
//   EventBus 이벤트(DB_WRITE_REQUESTED, TRAIN_COMPLETE_RECEIVED)를 구독하여
//   DAO를 통해 DB 작업을 수행한다.
//   직접 SQL을 실행하지 않고, DAO에 위임한다.
// ============================================================================
#pragma once

#include "core/event_bus.h"
#include "storage/connection_pool.h"
#include "storage/dao.h"

#include <memory>
#include <string>

namespace factory {

class DbManager {
public:
    /// ConnectionPool을 외부에서 주입받는다
    DbManager(EventBus& bus, ConnectionPool& pool);
    ~DbManager() = default;

    void register_handlers();

private:
    void on_db_write(const std::any& payload);
    void on_train_complete(const std::any& payload);

    EventBus&       event_bus_;
    ConnectionPool& pool_;

    // 테이블별 DAO
    InspectionDao inspection_dao_;
    AssemblyDao   assembly_dao_;
    ModelDao      model_dao_;
};

} // namespace factory
