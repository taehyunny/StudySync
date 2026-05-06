// ============================================================================
// db_manager.cpp — DB 이벤트 핸들러 (DAO 기반)
// ============================================================================
// 책임:
//   EventBus 의 DB 관련 이벤트를 받아 DAO 에 위임하고, 결과에 따라 후속 이벤트
//   (ACK/NACK/MODEL_RELOAD_REQUESTED) 를 발행한다.
//
// 구독 이벤트:
//   DB_WRITE_REQUESTED      → InspectionEvent 를 inspections 테이블에 INSERT
//                             (Station2 는 assemblies 테이블에도 추가 INSERT)
//   TRAIN_COMPLETE_RECEIVED → (의도적으로 구독하지 않음)
//                             TrainService/TrainHandler 가 이미 동일 처리를 하므로
//                             중복 INSERT/ACK 를 방지하기 위해 여기서는 건너뜀.
//                             과거에 중복 구독으로 이중 INSERT 버그가 있었음.
//
// 발행 이벤트:
//   DB_WRITE_COMPLETED (성공) → AckSender 가 ACK 회신
//   ACK_SEND_REQUESTED (실패) → AckSender 가 NACK 회신
//   MODEL_RELOAD_REQUESTED    → AckSender 가 추론서버에 새 모델 전송
//
// 트랜잭션 경계:
//   현재는 단일 INSERT 단위로 트랜잭션 보호 없음. 추론-결과와 assembly-결과를
//   같은 트랜잭션에 묶지 않음 — 정책상 Station2 에서 한쪽만 성공해도 로그만 남김.
// ============================================================================
#include "storage/db_manager.h"
#include "Protocol.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>

namespace factory {

DbManager::DbManager(EventBus& bus, ConnectionPool& pool)
    : event_bus_(bus),
      pool_(pool),
      inspection_dao_(pool),
      assembly_dao_(pool),
      model_dao_(pool) {
}

void DbManager::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_REQUESTED,
                         [this](const std::any& p) { this->on_db_write(p); });
    // TRAIN_COMPLETE_RECEIVED는 TrainHandler(Service 레이어)에서 처리
    // 여기서 중복 구독하면 이중 DB INSERT + 이중 ACK 발생
}

// ---------------------------------------------------------------------------
// on_db_write — 검사 결과(InspectionEvent) 를 DB 에 저장
//
// 흐름:
//   1) inspection_dao_.insert() → inspections 테이블에 기본 결과 INSERT
//      - 반환 auto_increment id 를 이후 assembly 참조 FK 로 사용
//   2) Station2 이면 assembly_dao_.insert() 로 assemblies 테이블에 추가 INSERT
//      - cap_ok/label_ok/fill_ok 등 조립 특화 필드
//      - 실패해도 NACK 보내지 않음 — main 검사결과는 이미 저장됐으므로
//   3) 성공 시 DB_WRITE_COMPLETED 발행 → AckSender 가 추론서버에 ACK
//   4) 실패 시 ACK_SEND_REQUESTED(ack_ok=false) 발행 → 추론서버에 NACK
// ---------------------------------------------------------------------------
void DbManager::on_db_write(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);

    long long inspection_id = inspection_dao_.insert(ev);
    if (inspection_id < 0) {
        // 메인 검사결과 저장 실패 — NACK 로 추론서버가 재전송 판단하도록
        log_err_db("INSERT inspections 실패");
        AckSendEvent nack{};
        nack.protocol_no   = static_cast<int>(
            ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
        nack.inspection_id = ev.inspection_id;
        nack.sender_addr   = ev.sender_addr;
        nack.ack_ok        = false;
        nack.error_message = "db_insert_failed";
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, nack);
        return;
    }

    // Station2 (조립검사) 전용 상세 필드를 별도 테이블에 저장
    // 실패해도 메인 inspections 는 이미 성공했으므로 경고만 남기고 계속 진행
    if (ev.station_id == static_cast<int>(StationId::ASSEMBLY)) {
        if (assembly_dao_.insert(ev, inspection_id) < 0) {
            log_err_db("INSERT assemblies 실패");
        }
    }

    // DB_WRITE_COMPLETED → AckSender::on_db_write_completed 가 ACK 전송
    event_bus_.publish(EventType::DB_WRITE_COMPLETED, ev);
}

// ---------------------------------------------------------------------------
// on_train_complete — (현재 비활성) 학습 완료 후처리
//
// 중요: 이 메서드는 현재 register_handlers 에서 구독되지 않음.
//       TrainHandler/TrainService 가 동일한 처리(파일 저장 + DB INSERT +
//       ACK + MODEL_RELOAD_REQUESTED)를 이미 담당하므로 여기서는 비활성.
//       역사적 참고용 + 만약 Service 레이어로 옮기지 않은 배포본에서
//       fallback 으로 쓰일 수 있음.
//
// 흐름(활성화 시):
//   1) model_bytes 를 ./storage/models/station{N}/{version}.{ext} 로 기록
//   2) models 테이블에 메타데이터 INSERT
//   3) 학습서버에 TRAIN_COMPLETE_ACK
//   4) 추론서버에 MODEL_RELOAD_REQUESTED (모델 바이너리 동봉)
// ---------------------------------------------------------------------------
void DbManager::on_train_complete(const std::any& payload) {
    const auto& ev = std::any_cast<const TrainCompleteEvent&>(payload);

    log_train("학습 완료 수신 | 모델=%s 버전=%s 정확도=%.4f 파일=%zu bytes",
              ev.model_type.c_str(), ev.version.c_str(), ev.accuracy,
              ev.model_bytes.size());

    // ── 모델 파일 바이너리를 로컬에 저장 ──
    // 확장자는 학습서버가 보낸 원본 model_path 에서 추출 (.ckpt / .pt 등)
    std::string saved_path;
    if (!ev.model_bytes.empty()) {
        std::string ext = ".bin";
        auto dot_pos = ev.model_path.rfind('.');
        if (dot_pos != std::string::npos) {
            ext = ev.model_path.substr(dot_pos);
        }

        std::string dir = "./storage/models/station" + std::to_string(ev.station_id);
        std::filesystem::create_directories(dir);
        saved_path = dir + "/" + ev.version + ext;

        std::ofstream ofs(saved_path, std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char*>(ev.model_bytes.data()),
                      static_cast<std::streamsize>(ev.model_bytes.size()));
            log_train("모델 파일 저장 완료 | %s (%zu bytes)",
                      saved_path.c_str(), ev.model_bytes.size());
        } else {
            log_err_train("모델 파일 저장 실패 | %s", saved_path.c_str());
            saved_path.clear();
        }
    }

    // DB에 모델 정보 INSERT (저장된 로컬 경로 사용)
    TrainCompleteEvent ev_copy = ev;
    if (!saved_path.empty()) {
        ev_copy.model_path = saved_path;
    }

    if (model_dao_.insert(ev_copy)) {
        log_db("INSERT models 성공");

        // 학습 서버에 ACK
        AckSendEvent ack{};
        ack.protocol_no    = static_cast<int>(ProtocolNo::TRAIN_COMPLETE_ACK);
        ack.inspection_id  = ev.request_id;
        ack.sender_addr    = ev.sender_addr;
        ack.ack_ok         = true;
        event_bus_.publish(EventType::ACK_SEND_REQUESTED, ack);

        // 추론서버에 모델 리로드 명령
        if (!ev.model_bytes.empty()) {
            ModelReloadEvent reload{};
            reload.station_id  = ev.station_id;
            reload.model_path  = saved_path.empty() ? ev.model_path : saved_path;
            reload.version     = ev.version;
            reload.model_type  = ev.model_type;
            reload.model_bytes = ev.model_bytes;
            event_bus_.publish(EventType::MODEL_RELOAD_REQUESTED, reload);
            log_train("추론서버 모델 리로드 요청 발행 | 스테이션=%d", ev.station_id);
        }
    } else {
        log_err_db("INSERT models 실패");
    }
}

} // namespace factory
