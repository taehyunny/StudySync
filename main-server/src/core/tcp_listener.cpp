// ============================================================================
// tcp_listener.cpp — AI(추론/학습) 서버 수신 리스너
// ============================================================================
// 책임:
//   AI 추론서버 / 학습서버가 메인(9000) 으로 접속해 보내는 패킷을 수신하여
//   EventBus 에 PACKET_RECEIVED 이벤트로 발행한다. 이후 Router 가 protocol_no
//   로 분기해 도메인 이벤트로 변환.
//
// 패킷 프로토콜:
//   [4바이트 JSON 길이(Big-Endian)] + [JSON 본문]
//     (+ [원본 이미지])   ← image_size > 0
//     (+ [히트맵 이미지]) ← heatmap_size > 0 (v0.9.0+)
//     (+ [마스크 이미지]) ← pred_mask_size > 0 (v0.9.0+)
//     (+ [모델 바이너리]) ← TRAIN_COMPLETE 시 image_size 에 모델 파일 크기
//
// 보안:
//   - IP 화이트리스트 (security::is_allowed_ip) 로 내부망만 허용
//   - 동시 AI 접속 수 10개 제한
//   - JSON 최대 64KB, 각 이미지 50MB 상한 — 비정상 대용량 차단
//
// TCP Keepalive (Linux):
//   공장 환경 특성상 유휴 연결(학습서버) 의 좀비화가 발생하기 쉬워 공격적으로
//   튜닝: 60초 유휴 → 10초 간격 probe → 3회 실패 시 dead.
//   OS 기본값(7200초) 대비 90초 내 좀비 감지.
//
// 스레드 모델:
//   accept 스레드 1개 + 접속당 detach 된 handle_client 스레드.
// ============================================================================

#include "core/tcp_listener.h"
#include "monitor/connection_registry.h"
#include "security/ip_filter.h"
#include "Protocol.h"

#include "core/logger.h"

#include <iostream>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  using socklen_t = int;
  #define CLOSE_SOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>  // TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCK ::close
#endif

namespace factory {

TcpListener::TcpListener(EventBus& bus, uint16_t port)
    : event_bus_(bus),
      listen_port_(port),
      server_fd_(-1),
      is_running_(false) {
}

TcpListener::~TcpListener() {
    stop();
}

void TcpListener::start() {
    if (is_running_.exchange(true)) return;

    server_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd_ < 0) {
        log_err_main("소켓 생성 실패 | AI수신 리스너");
        is_running_.store(false);
        return;
    }

    // SO_REUSEADDR: 서버 재시작 시 TIME_WAIT 상태 포트 재사용 허용
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // accept()에 1초 타임아웃 설정 — stop() 호출 시 accept 블로킹에서 빠져나오기 위함
    struct timeval tv{1, 0};
    ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_err_main("바인드 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }
    if (::listen(server_fd_, 16) < 0) {
        log_err_main("리슨 실패 | 포트=%d", listen_port_);
        CLOSE_SOCK(server_fd_);
        is_running_.store(false);
        return;
    }

    accept_thread_ = std::thread(&TcpListener::run_accept_loop, this);
    log_main("AI수신 리스너 시작 | 포트=%d", listen_port_);
}

void TcpListener::stop() {
    if (!is_running_.exchange(false)) return;
    if (server_fd_ >= 0) {
        CLOSE_SOCK(server_fd_);
        server_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void TcpListener::run_accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = static_cast<int>(
            ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_fd < 0) {
            // 타임아웃 또는 종료 시 무시하고 루프 재확인
            continue;
        }
        char ip_buf[64] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string client_ip(ip_buf);

        // IP 화이트리스트 검증 — 허용된 내부망 IP만 접속 가능
        if (!factory::security::is_allowed_ip(client_ip)) {
            log_err_main("비인가 IP 차단 | ip=%s", client_ip.c_str());
            CLOSE_SOCK(client_fd);
            continue;
        }

        // "IP:PORT" 형식으로 조합 — ACK 전송 시 ConnectionRegistry의 키로 사용
        std::string remote_addr = client_ip + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        // 동시 접속 수 제한 (최대 10개 AI서버)
        int conn_count = static_cast<int>(
            ConnectionRegistry::instance().get_all_connections().size());
        if (conn_count >= 10) {
            log_err_main("AI서버 최대 접속 수 초과 | 현재=%d", conn_count);
            CLOSE_SOCK(client_fd);
            continue;
        }

        std::thread(&TcpListener::handle_client, this, client_fd, remote_addr).detach();
    }
}

void TcpListener::handle_client(int client_fd, const std::string& remote_addr) {
    // recv 타임아웃 1시간 — Training 서버 같은 유휴 연결 유지
    // (Slow Loris 방어는 max_connections 상한으로 대체)
    // AI 추론서버는 지속 통신, Training은 학습 주기 단위로 통신
    struct timeval client_tv{3600, 0};
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&client_tv), sizeof(client_tv));

    // TCP Keepalive — 학습서버 같은 유휴 연결의 좀비화 방지
    // 공장 환경에 맞춰 공격적 튜닝: 60초 유휴 후 10초 간격 probe, 3회 실패 시 dead
    // → 최대 90초 내 좀비 연결 감지 (OS 기본 2시간 → 90초 단축)
    int keepalive = 1;
    ::setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE,
                 reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));
#ifdef __linux__
    int keepidle  = 60;   // 60초 유휴 후 probe 시작
    int keepintvl = 10;   // probe 간격 10초
    int keepcnt   = 3;    // 3회 무응답 시 dead 판정
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle,  sizeof(keepidle));
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT,  &keepcnt,  sizeof(keepcnt));
#endif

    ConnectionRegistry::instance().register_connection(remote_addr, client_fd);
    log_main("AI서버 연결 | fd=%d ip=%s", client_fd, remote_addr.c_str());

    while (is_running_.load()) {
        std::string          json_payload;
        std::vector<uint8_t> image_bytes;
        if (!recv_one_packet(client_fd, json_payload, image_bytes)) {
            break;
        }

        PacketReceivedEvent ev{};
        ev.json_payload = std::move(json_payload);
        ev.image_bytes  = std::move(image_bytes);
        ev.remote_addr  = remote_addr;
        if (!event_bus_.publish_critical(EventType::PACKET_RECEIVED, ev)) {
            log_err_main("TCP 패킷 큐잉 실패 | ACK 필요 가능 packet 드롭 addr=%s",
                         remote_addr.c_str());
        }
    }

    log_main("AI서버 연결 해제 | fd=%d ip=%s", client_fd, remote_addr.c_str());
    ConnectionRegistry::instance().unregister_connection(remote_addr);
    CLOSE_SOCK(client_fd);
}

// 정확히 n바이트를 수신할 때까지 반복 호출 (TCP 스트림 특성상 한 번에 안 올 수 있음)
// 연결 끊김 또는 에러 시 false 반환
static bool recv_n(int fd, void* buf, std::size_t n) {
    std::size_t total = 0;
    auto*       p     = static_cast<char*>(buf);
    while (total < n) {
        int got = static_cast<int>(::recv(fd, p + total, static_cast<int>(n - total), 0));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

// JSON 문자열에서 정수 필드를 단순 키워드 검색으로 추출한다.
// (nlohmann::json 전체 파싱은 Router에서 하므로 여기서는 경량 추출만 수행)
// 키가 없으면 0 반환 → 하위호환 (구버전 AI서버 패킷 처리용).
static std::size_t extract_size_field(const std::string& json, const char* key) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return static_cast<std::size_t>(std::strtoul(
        json.c_str() + colon + 1, nullptr, 10));
}

bool TcpListener::recv_one_packet(int client_fd,
                                  std::string& out_json,
                                  std::vector<uint8_t>& out_image) {
    // [4byte length(BE)] + [JSON] + [바이너리(옵션, image_size 바이트)]
    uint8_t header[HEADER_SIZE];
    if (!recv_n(client_fd, header, HEADER_SIZE)) return false;

    uint32_t json_size = (uint32_t)header[0] << 24 |
                         (uint32_t)header[1] << 16 |
                         (uint32_t)header[2] << 8  |
                         (uint32_t)header[3];

    if (json_size == 0 || json_size > 64 * 1024) {
        log_err_main("잘못된 JSON 크기 | size=%u", json_size);
        return false;
    }

    out_json.assign(json_size, '\0');
    if (!recv_n(client_fd, out_json.data(), json_size)) return false;

    // 바이너리 (TRAIN_COMPLETE 시 모델 파일 등) — 500MB 상한.
    const std::size_t image_size = extract_size_field(out_json, "\"image_size\"");
    constexpr std::size_t MAX_BLOB = 500ULL * 1024 * 1024;
    if (image_size > MAX_BLOB) {
        log_err_main("바이너리 크기 초과 | size=%zu", image_size);
        return false;
    }
    if (image_size > 0) {
        out_image.resize(image_size);
        if (!recv_n(client_fd, out_image.data(), image_size)) return false;
    }
    return true;
}

} // namespace factory
