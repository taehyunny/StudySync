// ============================================================================
// router.h — 패킷 라우팅 (프로토콜 번호별 이벤트 분기)
// ============================================================================
// 목적:
//   TCP 수신 계층(PacketReader)이 발행한 PACKET_RECEIVED 이벤트를 구독하여
//   JSON 페이로드 내 protocol_no 필드를 기준으로 적절한 도메인 이벤트
//   (INSPECTION_INBOUND, INSPECTION_ASSEMBLY, OK_COUNT_RECEIVED 등)로
//   재발행하는 중앙 라우터 역할을 한다.
//
// 설계 결정:
//   - nlohmann::json 등 외부 라이브러리 의존 없이, 경량 수동 파서(extract_*)로
//     필요한 필드만 추출한다. 패킷 구조가 단순하고 성능이 중요하기 때문.
//   - Router 자체는 비즈니스 로직을 갖지 않으며, 순수 분기 + 이벤트 변환만 수행.
// ============================================================================
#pragma once

#include "core/event_bus.h"

namespace factory {

/// 수신 패킷을 protocol_no 기준으로 분류하여 도메인 이벤트로 재발행하는 라우터
class Router {
public:
    /// @param bus  전역 EventBus 참조 — 이벤트 구독/발행에 사용
    explicit Router(EventBus& bus);

    /// EventBus에 PACKET_RECEIVED 핸들러를 등록한다.
    /// 서버 초기화 시 한 번만 호출해야 한다.
    void register_handlers();

private:
    /// PACKET_RECEIVED 이벤트 콜백.
    /// JSON에서 protocol_no를 파싱한 뒤 switch-case로 적절한 이벤트 구조체를
    /// 생성하여 EventBus에 발행한다.
    void on_packet_received(const std::any& payload);

    // ---- 경량 JSON 필드 추출 유틸리티 ----
    // 외부 JSON 라이브러리 없이 "key": value 형태를 직접 탐색한다.
    // 중첩 객체나 배열은 지원하지 않으며, 1-depth 평탄 JSON 전용이다.

    /// 문자열 값 추출 (키가 없으면 빈 문자열 반환)
    static std::string extract_str(const std::string& json, const std::string& key);
    /// 정수 값 추출 (키가 없으면 0 반환)
    static int         extract_int(const std::string& json, const std::string& key);
    /// 실수 값 추출 (키가 없으면 0.0 반환)
    static double      extract_double(const std::string& json, const std::string& key);

    EventBus& event_bus_;   ///< 전역 이벤트 버스 참조
};

} // namespace factory
