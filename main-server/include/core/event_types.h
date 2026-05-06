#pragma once
// ============================================================================
// event_types.h — 이벤트 타입 열거형 및 페이로드 구조체 정의
// ============================================================================
// 목적:
//   EventBus에서 사용하는 모든 이벤트 종류(EventType)와
//   각 이벤트에 첨부되는 데이터 구조체(페이로드)를 정의한다.
//
// 설계 원칙:
//   - 이벤트 타입은 enum class로 타입 안전성을 확보한다.
//   - 페이로드 구조체는 값 타입이며, EventBus에 std::any로 감싸져 전달된다.
//   - 핸들러에서 std::any_cast<해당 구조체>로 복원하여 사용한다.
//
// 이벤트 흐름 — 검사 경로:
//   PACKET_RECEIVED → Router 가 프로토콜 번호로 분류
//     → INSPECTION_INBOUND / INSPECTION_ASSEMBLY (Station1/Station2)
//        → InspectionService.process() 가 단일 진입점으로 검증/저장/DB INSERT 수행
//           → DB_WRITE_COMPLETED → AckSender 가 추론서버로 ACK 응답
//           → GUI_PUSH_REQUESTED → GuiNotifier 가 클라이언트에 NG 푸시
//
// 이벤트 흐름 — 학습 경로:
//   PACKET_RECEIVED → Router
//     → TRAIN_PROGRESS_RECEIVED → GuiNotifier 가 진행률 푸시
//     → TRAIN_COMPLETE_RECEIVED → TrainService 가 파일 저장 + DB INSERT
//                                 → ACK_SEND_REQUESTED (학습서버)
//                                 → MODEL_RELOAD_REQUESTED → AckSender 가
//                                   추론서버로 새 모델 바이너리 송신
//     → TRAIN_FAIL_RECEIVED → GuiNotifier 가 실패 알림 푸시
//
// 이벤트 흐름 — 헬스체크 경로 (v0.11.0 동적 감지):
//   HealthChecker 주기 tick → ConnectionRegistry 의 server_type 검사
//     → 상태 전환 시 SERVER_DOWN / SERVER_RECOVERED 발행
//        → GuiNotifier 가 HEALTH_PUSH 로 클라이언트 LED 갱신
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>

namespace factory {

// ---------------------------------------------------------------------------
// EventType 열거형
// ---------------------------------------------------------------------------
// 이벤트 파이프라인의 각 단계를 나타낸다.
// 프로토콜 번호(1000, 1002, 1102 등)는 AI 추론/학습 서버와의 TCP 프로토콜 번호.
// ---------------------------------------------------------------------------
enum class EventType : int {
    // ── TCP 수신 계층 ──
    PACKET_RECEIVED = 0,      // 추론서버로부터 raw 패킷 수신 → Router가 구독

    // ── 라우팅 후 (Router가 프로토콜 번호로 분류하여 재발행) ──
    INSPECTION_INBOUND,       // 입고 검사 결과 도착 (프로토콜 1000)
    INSPECTION_ASSEMBLY,      // 조립 검사 결과 도착 (프로토콜 1002)

    // ── 검증 통과 → 백그라운드 영속화 (v0.12.0 분리) ──
    // StationHandler 가 validate_only() 만 하고 즉시 ACK 를 쏘고,
    // 실제 이미지 저장 + DB INSERT 는 이 이벤트를 구독한 InspectionService 워커가
    // 별도 스레드에서 비동기 처리. AI 서버 ACK 타임아웃을 원천 회피.
    INSPECTION_VALIDATED,

    // ── 후처리 ──
    DB_WRITE_COMPLETED,       // (legacy) Service 성공 신호 — v0.12.0 부터 AckSender 미사용
    GUI_PUSH_REQUESTED,       // Service 성공 → GuiNotifier가 클라이언트 푸시

    // ── ACK / 메타 정보 ──
    ACK_SEND_REQUESTED,       // 추론서버로 ACK/NACK 응답 송신 요청
    OK_COUNT_RECEIVED,        // 양품 카운트 집계 수신 (프로토콜 1004)
    INSPECT_META_RECEIVED,    // 검사 메타정보 수신 (프로토콜 1006, OK/NG 공통)

    // ── 학습 서버 이벤트 ──
    TRAIN_PROGRESS_RECEIVED,  // 학습 진행률 수신 (프로토콜 1102)
    TRAIN_COMPLETE_RECEIVED,  // 학습 완료 수신 (프로토콜 1104) → DB 저장 + GUI 푸시
    TRAIN_FAIL_RECEIVED,      // 학습 실패 수신 (프로토콜 1106)
    MODEL_RELOAD_REQUESTED,   // 모델 리로드 요청 → 추론서버에 CMD + 바이너리 전송

    // ── 헬스체크 ──
    HEALTH_CHECK_TICK,        // HealthChecker의 주기적 트리거 (5초 간격)
    SERVER_DOWN,              // 추론/학습 서버 연결 불가 감지
    SERVER_RECOVERED,         // 장애 서버 복구 감지

    // ── 시스템 ──
    SYSTEM_SHUTDOWN,          // 서버 종료 신호
};

// ---------------------------------------------------------------------------
// 페이로드 구조체들
// ---------------------------------------------------------------------------
// 각 구조체는 특정 EventType에 1:1 대응한다.
// EventBus::publish() 시 std::any로 감싸지고,
// 핸들러에서 std::any_cast<구조체>로 복원한다.
// ---------------------------------------------------------------------------

// PACKET_RECEIVED 페이로드 — TcpListener가 생성
// NG 결과(1000/1002)일 경우 최대 3장의 이미지가 순서대로 담길 수 있다:
//   image_bytes       — 원본 JPEG
//   heatmap_bytes     — 원본+히트맵(Anomaly Map) PNG
//   pred_mask_bytes   — 원본+윤곽선(Pred Mask) PNG
// 각 _size 필드가 0이면 해당 바이너리는 비어 있다 (하위호환).
struct PacketReceivedEvent {
    std::string  json_payload;          // JSON 본문
    std::vector<uint8_t> image_bytes;    // 원본 이미지 바이너리
    std::vector<uint8_t> heatmap_bytes;  // 히트맵 오버레이 PNG (v0.9.0+)
    std::vector<uint8_t> pred_mask_bytes;// Pred Mask 오버레이 PNG (v0.9.0+)
    std::string  remote_addr;           // 송신자 IP
};

// INSPECTION_INBOUND / INSPECTION_ASSEMBLY 공통 페이로드 — Router가 생성
// 입고검사(1000)와 조립검사(1002) 모두 동일 구조체를 사용한다.
// protocol_no로 어떤 검사인지 구분.
struct InspectionEvent {
    int          protocol_no     = 0;     // 프로토콜 번호 (1000: 입고, 1002: 조립)
    std::string  inspection_id;            // 추론서버 발급 고유 ID (ACK 매칭 키)
    int          station_id      = 0;      // 검사 스테이션 번호
    std::string  result;                   // 판정 결과 ("OK" / "NG")
    std::string  defect_type;              // NG 시 불량 유형 (예: "scratch")
    double       score           = 0.0;    // AI 추론 신뢰도 (0.0 ~ 1.0)
    int          latency_ms      = 0;      // 추론 소요 시간 (ms)
    std::string  timestamp;                // 검사 시각 (ISO8601)
    std::vector<uint8_t> image_bytes;      // NG 원본 이미지 바이너리 (OK이면 빈 벡터)
    std::vector<uint8_t> heatmap_bytes;    // NG 히트맵(Anomaly Map) 오버레이 PNG (v0.9.0+)
    std::vector<uint8_t> pred_mask_bytes;  // NG Pred Mask 오버레이 PNG (v0.9.0+)
    std::string  raw_json;                 // 원본 JSON (조립 검사 시 부가정보 추출용)
    std::string  sender_addr;              // ACK 회신 대상 주소 ("IP:PORT")
    long long    db_id            = -1;    // v0.14.7: DB inspections row id (INSERT 후 채움 → GUI 푸시에 포함)
};

// ACK_SEND_REQUESTED 페이로드 — DB 기록 완료 후 AckSender가 소비
// sender_addr로 ConnectionRegistry에서 소켓 FD를 조회하여 응답 전송.
struct AckSendEvent {
    int         protocol_no = 0;          // 송신할 ACK 프로토콜 번호 (1001/1003 등)
    std::string inspection_id;             // 요청-응답 매칭용 ID
    std::string sender_addr;               // 회신 대상 ("IP:PORT")
    bool        ack_ok       = true;       // true=ACK, false=NACK
    std::string error_message;             // NACK 사유 메시지
};

// OK_COUNT_RECEIVED 페이로드 (프로토콜 1004) — 일정 주기별 양품/불량 집계
struct OkCountEvent {
    int         station_id    = 0;
    int         ok_count      = 0;
    int         ng_count      = 0;
    double      latency_avg   = 0.0;
    std::string period;
};

// INSPECT_META_RECEIVED 페이로드 (프로토콜 1006)
// OK/NG 공통으로 전송되는 메타정보 — DB inspections 테이블의 기본 행 기록에 사용
struct InspectMetaEvent {
    std::string inspection_id;             // 검사 고유 ID
    int         station_id  = 0;           // 스테이션 번호
    std::string timestamp;                 // 검사 시각
    int         latency_ms  = 0;           // 추론 소요 시간
    int         model_id    = 0;           // 사용된 AI 모델 ID
    std::string result;                    // "ok" / "ng"
};

// TRAIN_PROGRESS_RECEIVED 페이로드 (프로토콜 1102)
// 학습 서버가 주기적으로 보내는 학습 진행 상황 — GUI에 실시간 표시용
struct TrainProgressEvent {
    std::string request_id;
    int         station_id   = 0;
    std::string model_type;        // "PatchCore" / "YOLO11"
    int         progress     = 0;  // 0~100%
    int         epoch        = 0;
    double      loss         = 0.0;
    std::string status;            // 상태 설명
    std::string sender_addr;
};

// TRAIN_COMPLETE_RECEIVED 페이로드 (프로토콜 1104)
// 학습 완료 시 모델 경로, 정확도 등을 포함 — DB에 모델 정보 저장 + GUI 알림
struct TrainCompleteEvent {
    std::string request_id;
    int         station_id   = 0;
    std::string model_type;
    std::string model_path;                    // 학습서버 측 원본 경로
    std::string version;
    double      accuracy     = 0.0;
    std::string message;
    std::string sender_addr;
    std::vector<uint8_t> model_bytes;          // 모델 파일 바이너리 (TCP로 수신)
};

// TRAIN_FAIL_RECEIVED 페이로드 (프로토콜 1106)
// 학습 실패 시 에러 코드와 메시지 포함 — GUI에 실패 알림
struct TrainFailEvent {
    std::string request_id;
    int         station_id   = 0;
    std::string model_type;
    std::string error_code;
    std::string message;
    std::string version;
    std::string sender_addr;
};

// MODEL_RELOAD_REQUESTED 페이로드
// 학습 완료 후 추론서버에 새 모델을 전송하기 위한 이벤트
struct ModelReloadEvent {
    int         station_id   = 0;
    std::string model_path;                    // 메인서버 로컬 저장 경로
    std::string version;
    std::string model_type;
    std::vector<uint8_t> model_bytes;          // 모델 파일 바이너리
};

// HEALTH_CHECK_TICK 페이로드 (없음)

// SERVER_DOWN / SERVER_RECOVERED 페이로드
// HealthChecker가 TCP 연결 시도로 장애/복구를 감지하여 발행
struct ServerStatusEvent {
    std::string server_name;   // 서버 식별 이름 (예: "ai_inference_1")
    std::string ip;            // 서버 IP
    uint16_t    port = 0;      // 서버 포트
};

} // namespace factory
