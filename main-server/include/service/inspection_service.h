// ============================================================================
// inspection_service.h — 검사 결과 처리 서비스 (v0.12.0: 비동기 분리)
// ============================================================================
// 책임:
//   AI 추론서버의 NG 패킷을 받아 "검증 → ACK → 백그라운드 저장" 구조로 처리.
//
//   v0.11.0 까지는 StationHandler 가 process() 를 동기 호출 → 저장 끝난 뒤
//   ACK 를 보냈지만, 이미지 저장(수 MB 디스크 I/O) + DB INSERT 가 1초를 넘어
//   AI 서버 ACK 타임아웃을 유발했다.
//
//   v0.12.0 부터:
//     1) StationHandler 는 validate_only() 만 호출 후 즉시 ACK 발행
//     2) INSPECTION_VALIDATED 이벤트를 publish → EventBus 백그라운드 워커가
//        본 서비스의 on_validated() 를 호출해 이미지 저장 + DB INSERT + GUI 푸시
//     3) 백그라운드 실패 시: 프로세스가 이미 ACK 를 성공 응답했으므로
//        "sliced failure" — 별도 에러 로그 + 운영자 알림만 남기고 재전송은 하지 않음.
//
// 호출자:
//   - StationHandler::on_inspection → validate_only
//   - EventBus INSPECTION_VALIDATED 구독자 = 본 서비스의 on_validated
//
// 사용 DAO: InspectionDao, AssemblyDao
// ============================================================================
#pragma once

#include "storage/dao.h"
#include "core/event_bus.h"
#include "core/event_types.h"

#include <string>
#include <vector>

namespace factory {

// 백그라운드 persist 결과 — 로깅 용도
struct InspectionResult {
    bool        success = false;
    long long   inspection_id = -1;
    std::string image_path;        // 원본 JPEG 저장 경로
    std::string heatmap_path;      // Anomaly Map PNG 저장 경로 (v0.9.0+)
    std::string pred_mask_path;    // Pred Mask PNG 저장 경로 (v0.9.0+)
    std::string error_message;
};

class InspectionService {
public:
    InspectionService(EventBus& bus,
                      ConnectionPool& pool,
                      const std::string& image_root_dir);

    /// EventBus 에 INSPECTION_VALIDATED 구독 등록. main.cpp 에서 1회 호출.
    void register_handlers();

    /// 빠른 검증만 수행 (I/O 없음, <1ms). StationHandler 가 ACK 결정용으로 호출.
    ///   반환값 true: 검증 통과 → 호출자는 ACK(ok) + INSPECTION_VALIDATED 를 발행
    ///   반환값 false: 검증 실패 → 호출자는 NACK(error_message 포함) 를 발행
    /// 부수효과: 내부에서 ev.result 를 소문자 정규화 ("OK"→"ok") 한다.
    bool validate_only(InspectionEvent& ev, std::string& out_error);

private:
    /// INSPECTION_VALIDATED 구독 콜백. 백그라운드 워커 스레드에서 호출됨.
    /// 이미지 저장 → DB INSERT → GUI 푸시 순으로 진행하고,
    /// 실패는 로그만 남긴다 (이미 ACK 는 발행된 뒤이므로 재전송 불가).
    void on_validated(const std::any& payload);

    /// 실제 영속화 로직 (on_validated 에서 호출).
    InspectionResult persist(const InspectionEvent& ev);

    /// 실제 검증 로직 (validate_only 가 호출하는 내부 메서드).
    /// ev.result 를 소문자로 정규화하는 부수효과가 있다.
    bool validate(InspectionEvent& ev, std::string& out_error);

    /// 이미지 저장 공통 헬퍼 — 임의의 바이너리를 지정 확장자로 저장한다.
    ///   inspection_id: 검사 유일 ID (파일명에 포함해 병렬 저장 경합 원천 차단).
    ///   suffix: "original", "heatmap", "mask" 등 파일명 구분자
    ///   ext:    ".jpg", ".png" 등 확장자
    std::string save_blob(int station_id,
                          const std::string& timestamp,
                          const std::string& inspection_id,
                          const std::vector<uint8_t>& bytes,
                          const std::string& suffix,
                          const std::string& ext);

    EventBus&     event_bus_;
    InspectionDao inspection_dao_;
    AssemblyDao   assembly_dao_;
    std::string   image_root_dir_;
};

} // namespace factory
