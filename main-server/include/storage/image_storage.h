#pragma once
// ============================================================================
// image_storage.h — NG 이미지 파일 저장 관리자
// ============================================================================
// 목적:
//   AI 추론 서버가 NG(불량) 판정 시 동봉한 이미지 바이너리를 로컬 파일로 저장.
//   IMAGE_SAVE_REQUESTED 이벤트를 구독하여 비동기로 동작한다.
//
// 저장 경로 규칙:
//   {root_dir}/station{N}/{YYYYMMDD}/ng_{밀리초타임스탬프}.jpg
//   예: ./storage/station1/20260417/ng_1713340800123.jpg
//
// 설계 의도:
//   날짜별 디렉터리 분리로 대량 이미지 관리를 용이하게 하고,
//   DB의 image_path 컬럼과 동일한 경로 규칙을 사용하여 일관성 유지.
// ============================================================================

#include "core/event_bus.h"

#include <string>

namespace factory {

class ImageStorage {
public:
    /// @param root_dir 이미지 저장 루트 디렉터리 (예: "./storage")
    ImageStorage(EventBus& bus, const std::string& root_dir);

    /// EventBus에 IMAGE_SAVE_REQUESTED 핸들러를 등록한다.
    void register_handlers();

private:
    /// 실제 이미지 파일 쓰기를 수행하는 이벤트 콜백
    void on_image_save(const std::any& payload);

    /// 저장 경로를 조합한다. timestamp에서 YYYYMMDD를 추출하여 날짜별 디렉터리 구성.
    static std::string make_save_path(const std::string& root_dir,
                                      int station_id,
                                      const std::string& timestamp);

    EventBus&   event_bus_;
    std::string root_dir_;
};

} // namespace factory
