// ============================================================================
// tcp_utils.h — TCP 전송 유틸리티
// ============================================================================
// send_all: partial send를 처리하는 안전한 전송 함수
// send_frame: [4바이트 BE 길이 헤더] + [데이터]를 원자적으로 전송
// ============================================================================
#pragma once

#include <cstdint>   // uint32_t, uint8_t (고정 크기 정수 타입)
#include <string>    // std::string
#include <vector>    // std::vector (미사용이지만 포함된 파일이 쓸 수도)

#ifdef _WIN32
  #include <winsock2.h>   // Windows 소켓 API (send, MSG_NOSIGNAL 등)
#else
  #include <sys/socket.h> // POSIX 소켓 API (Linux/Mac)
  #include <cerrno>       // errno 변수 (시스템 콜 에러 코드)
#endif

namespace factory {

/// 지정된 바이트를 모두 전송할 때까지 재시도. 실패 시 false.
/// 목적: TCP send()가 요청한 모든 바이트를 한 번에 보내지 못할 수 있음 (partial send).
///       큰 데이터(이미지/모델)나 네트워크 혼잡 시 발생하는 현상.
inline bool send_all(int fd, const void* data, std::size_t len) {
    // void*를 바이트 포인터(char*)로 변환해 산술 연산 가능하게
    const char* ptr = static_cast<const char*>(data);
    std::size_t remaining = len;       // 아직 보내지 못한 바이트 수

    while (remaining > 0) {            // 전부 보낼 때까지 반복
        // send() 시스템 콜:
        //   fd: 소켓 파일 디스크립터
        //   ptr: 보낼 데이터 시작 위치
        //   remaining: 남은 바이트 수 (int로 캐스팅, 2GB 이내)
        //   MSG_NOSIGNAL: 연결 끊긴 소켓에 write 시 SIGPIPE 신호 발생 방지
        //                 (신호 오면 프로세스 종료됨 → 반드시 설정)
        //   반환: 실제 보낸 바이트 수 (음수: 에러, 0: 연결 종료, 양수: 부분 전송 가능)
        ssize_t sent = ::send(fd, ptr, static_cast<int>(remaining), MSG_NOSIGNAL);

        if (sent <= 0) {
            // EINTR: 시스템 콜이 신호에 의해 중단됨 (Ctrl+C, SIGALRM 등) → 재시도
            if (sent < 0 && errno == EINTR) continue;
            return false;              // 그 외: 연결 끊김/프로토콜 오류/버퍼 고갈
        }

        ptr += sent;                   // 다음 전송 위치로 포인터 이동
        remaining -= static_cast<std::size_t>(sent);  // 남은 바이트 감소
    }
    return true;                       // 전체 전송 성공
}

/// [4바이트 BE 헤더] + [JSON 본문] 전송
/// 프로토콜 설계: 수신 측이 헤더만 먼저 읽어 본문 길이를 미리 알 수 있게 함.
/// BE (Big-Endian): 네트워크 표준 바이트 순서 (최상위 바이트 먼저)
inline bool send_json_frame(int fd, const std::string& json_body) {
    // 본문 크기를 uint32_t로 (최대 4GB, 실제로는 64KB 제한)
    uint32_t json_size = static_cast<uint32_t>(json_body.size());

    // 4바이트 헤더를 빅엔디안으로 수동 패킹
    // 비트 연산으로 최상위 바이트부터 추출:
    //   >> 24: 최상위 8비트를 최하위로 이동
    //   & 0xFF: 하위 8비트만 남기기 (나머지 상위 24비트 마스킹)
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),  // 바이트 3 (MSB)
        static_cast<uint8_t>((json_size >> 16) & 0xFF),  // 바이트 2
        static_cast<uint8_t>((json_size >>  8) & 0xFF),  // 바이트 1
        static_cast<uint8_t>( json_size        & 0xFF),  // 바이트 0 (LSB)
    };

    // 헤더 4바이트 → 본문 순서대로 전송 (원자성 보장 위해 연속 호출)
    if (!send_all(fd, header, 4)) return false;         // 헤더 실패 시 즉시 중단
    return send_all(fd, json_body.c_str(), json_body.size());  // 본문 전송 성공 여부 반환
}

} // namespace factory
