#pragma once
// ============================================================================
// tcp_listener.h — TCP 수신 리스너 (AI 추론 서버 → 메인 서버)
// ============================================================================
// 목적:
//   AI 추론 서버(입고검사/조립검사/학습 등)가 보내는 검사 결과 패킷을
//   TCP로 수신하여 PACKET_RECEIVED 이벤트로 EventBus에 발행한다.
//
// 패킷 프로토콜:
//   [4바이트 JSON 길이(Big-Endian)] + [JSON 본문] + [바이너리(옵션)]
//   바이너리 크기는 JSON 의 "image_size" 필드로 판단. 0 또는 누락이면 비어 있음.
//   StudySync 도메인 메시지는 본문 없음. TRAIN_COMPLETE 시에만 모델 바이너리 동봉.
//
// 스레드 구조:
//   - accept 스레드 1개: 클라이언트 연결 수락
//   - 클라이언트당 detach 스레드 1개: 패킷 수신 루프
//     (추론 서버 수가 소수이므로 스레드 풀 없이 간단 구조 채택)
// ============================================================================

#include "core/event_bus.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace factory {

// ---------------------------------------------------------------------------
// TcpListener 클래스
// ---------------------------------------------------------------------------
// EventBus 참조를 보유하며, 수신한 패킷을 PACKET_RECEIVED 이벤트로 발행.
// Router가 이 이벤트를 구독하여 프로토콜 번호별로 재분류(라우팅)한다.
// ---------------------------------------------------------------------------
class TcpListener {
public:
    /// @param bus  이벤트 발행에 사용할 EventBus 참조
    /// @param port 리슨할 TCP 포트 번호 (Protocol.h의 MAIN_SERVER_PORT)
    TcpListener(EventBus& bus, uint16_t port);
    ~TcpListener();   // 소멸 시 자동으로 stop() 호출

    /// 서버 소켓 바인드 + accept 스레드 기동
    void start();
    /// 서버 소켓 닫고 accept 스레드 join
    void stop();

private:
    /// accept 루프 — 클라이언트 연결마다 handle_client 스레드 생성
    void run_accept_loop();

    /// 단일 클라이언트 세션 처리 — 연결 유지하며 패킷 반복 수신
    /// @param client_fd   accept된 소켓 디스크립터
    /// @param remote_addr "IP:PORT" 문자열 (ACK 회신 라우팅 키로 사용)
    void handle_client(int client_fd, const std::string& remote_addr);

    /// 패킷 한 건 수신: length-prefixed JSON + 선택적 단일 바이너리 (모델 등)
    /// @return false 시 연결 종료 또는 프로토콜 오류 → 세션 종료
    bool recv_one_packet(int client_fd,
                         std::string& out_json,
                         std::vector<uint8_t>& out_image);

    EventBus&         event_bus_;       // 이벤트 발행용 버스 참조
    uint16_t          listen_port_;     // 리슨 포트
    int               server_fd_;       // 서버 소켓 FD (-1이면 미생성)
    std::thread       accept_thread_;   // accept 루프 스레드
    std::atomic<bool> is_running_;      // 종료 플래그
};

} // namespace factory
