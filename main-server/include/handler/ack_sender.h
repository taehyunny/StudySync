// ============================================================================
// ack_sender.h — 추론서버 ACK 응답 송신기
// ============================================================================
// 목적:
//   검사 결과가 DB에 기록된 뒤(DB_WRITE_COMPLETED) 또는 즉시 ACK가 필요한
//   경우(ACK_SEND_REQUESTED) 추론서버에 ACK 패킷을 회신한다.
//
// 흐름:
//   1. DB_WRITE_COMPLETED → InspectionEvent에서 sender_addr 추출
//   2. ConnectionRegistry로 sender_addr → client_fd 조회
//   3. [4byte BE length][JSON body] 형식으로 ACK 전송
//
// ACK 프로토콜 번호:
//   - STATION1_NG_ACK (1001) : 입고검사 NG 응답
//   - STATION2_NG_ACK (1003) : 조립검사 NG 응답
// ============================================================================
#pragma once

#include "core/event_bus.h"

namespace factory {

/// DB 기록 완료 또는 명시적 요청 시 추론서버로 ACK를 송신하는 핸들러
class AckSender {
public:
    /// @param bus  전역 EventBus 참조
    explicit AckSender(EventBus& bus);

    /// DB_WRITE_COMPLETED, ACK_SEND_REQUESTED 이벤트에 대한 핸들러를 등록한다.
    void register_handlers();

private:
    /// DB 기록 성공 시 호출 — 원래 요청의 protocol_no에 대응하는 ACK 번호를 구해 전송
    void on_db_write_completed(const std::any& payload);

    /// 에러 발생 등으로 즉시 ACK(ack_ok=false)를 보내야 할 때 호출
    void on_ack_send_requested(const std::any& payload);

    /// 실제 소켓 전송 로직.
    /// @param sender_addr   추론서버의 "ip:port" 문자열 (ConnectionRegistry 키)
    /// @param protocol_no   ACK 프로토콜 번호 (예: 1001, 1003)
    /// @param inspection_id 검사 ID — 추론서버가 요청-응답을 매칭하는 데 사용
    /// @param ack_ok        처리 성공 여부 (false면 error_message 포함)
    /// @param error_message ack_ok=false일 때 실패 사유
    /// @return 전송 성공 여부
    bool send_ack(const std::string& sender_addr,
                  int protocol_no,
                  const std::string& inspection_id,
                  bool ack_ok,
                  const std::string& error_message);

    /// MODEL_RELOAD_REQUESTED 이벤트 수신 시 추론서버에 모델 바이너리 전송
    void on_model_reload_requested(const std::any& payload);

    /// 추론서버에 MODEL_RELOAD_CMD(1010) + 모델 바이너리 전송
    /// @param model_type "PatchCore" 또는 "YOLO11" — 추론서버가 교체할 슬롯 구분용
    bool send_model_reload(int station_id,
                           const std::string& model_type,
                           const std::string& model_path,
                           const std::string& version,
                           const std::vector<uint8_t>& model_bytes);

    EventBus& event_bus_;
};

} // namespace factory
