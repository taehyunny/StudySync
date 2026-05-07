// ============================================================================
// main.cpp — StudySync 메인 운영 서버 진입점
// ============================================================================
// 목적:
//   EventBus 를 중심으로 컴포넌트를 생성·연결한 뒤,
//   AI 추론서버 측 TCP 리스너와 헬스체커를 기동한다.
//
// 아키텍처 요약 (옵션 B 단계 — TCP/AI 측만 와이어링):
//   EventBus (Pub/Sub)
//     ├─ TcpListener     : AI 추론서버 패킷 수신 → PACKET_RECEIVED
//     ├─ Router          : PACKET_RECEIVED 구독 → 도메인 이벤트 재발행
//     ├─ FocusService    : FOCUS_LOG_PUSH_RECEIVED → focus_logs INSERT + ACK
//     ├─ PostureService  : POSTURE_LOG / POSTURE_EVENT / BASELINE → DB + ACK
//     ├─ TrainHandler    : TRAIN_COMPLETE_RECEIVED → 모델 저장 + DB + 리로드 요청
//     ├─ AckSender       : ACK_SEND_REQUESTED + MODEL_RELOAD_REQUESTED → AI 측 송신
//     └─ HealthChecker   : 추론/학습 서버 주기적 생존 확인
//
//   클라(MFC) 측 HTTP 서버는 이번 단계 범위 밖 — 옵션 A/B 후속.
//
// 종료 흐름:
//   SIGINT/SIGTERM → g_should_exit → 메인 루프 탈출 → 역순 정리
// ============================================================================

#include "core/event_bus.h"
#include "core/tcp_listener.h"
#include "core/config.h"
#include "handler/router.h"
#include "handler/ack_sender.h"
#include "handler/train_handler.h"
#include "service/focus_service.h"
#include "service/posture_service.h"
#include "service/train_service.h"
#include "storage/connection_pool.h"
#include "monitor/health_checker.h"
#include "Protocol.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_should_exit{false};
void on_signal(int) { g_should_exit.store(true); }
} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    using namespace factory;

    // ────────────────────────────────────────────────────────────
    // 0) 설정 로드 (인자 > 환경변수 > 기본값)
    //    실행: ./studysync_main_server ../../config/config.json
    //    또는:  CONFIG_PATH=/path/config.json ./studysync_main_server
    // ────────────────────────────────────────────────────────────
    std::string config_path = "../../config/config.json";
    if (argc > 1) {
        config_path = argv[1];
    } else if (const char* env = std::getenv("CONFIG_PATH")) {
        config_path = env;
    }
    if (!Config::instance().load(config_path)) {
        std::cerr << "설정 로드 실패: " << config_path << std::endl;
        return 1;
    }

    auto& cfg = Config::instance();
    uint16_t    ai_port      = static_cast<uint16_t>(cfg.get_int("network.main_server_ai_port", 9000));
    std::string db_host      = cfg.get_str("database.host",     "10.10.10.100");
    std::string db_user      = cfg.get_str("database.user",     "admin");
    std::string db_password  = cfg.get_str("database.password", "1234");
    std::string db_schema    = cfg.get_str("database.schema",   "StudySync");
    int         db_pool_size = cfg.get_int("database.pool_size", 4);
    int         worker_count = 4;  // DB / ACK / Train 병렬 워커

    // 1) EventBus 시작
    EventBus event_bus(worker_count);
    event_bus.start();

    // 2) Router 등록 — TCP 패킷 → 도메인 이벤트
    Router router(event_bus);
    router.register_handlers();

    // 3) DB 커넥션 풀
    ConnectionPool db_pool(db_host, db_user, db_password, db_schema, 3306, db_pool_size);
    if (!db_pool.init()) {
        std::cerr << "DB 커넥션 풀 초기화 실패" << std::endl;
        return 1;
    }

    // 4) 도메인 서비스 — DB INSERT + ACK 발행
    FocusService focus_service(event_bus, db_pool);
    focus_service.register_handlers();

    PostureService posture_service(event_bus, db_pool);
    posture_service.register_handlers();

    // 5) 학습 채널
    TrainService train_service(db_pool);
    TrainHandler train_handler(event_bus, train_service);
    train_handler.register_handlers();

    // 6) ACK 송신기 — ACK_SEND_REQUESTED + MODEL_RELOAD_REQUESTED 구독
    AckSender ack_sender(event_bus);
    ack_sender.register_handlers();

    // 7) AI 측 TCP 리스너 시작 (포트 9000)
    TcpListener tcp_listener(event_bus, ai_port);
    tcp_listener.start();

    // 8) 헬스체크 — config.health_targets 로드
    std::vector<HealthTarget> targets;
    for (const auto& tc : cfg.get_health_targets()) {
        targets.push_back({tc.name, tc.ip, static_cast<uint16_t>(tc.port)});
    }
    HealthChecker health_checker(event_bus, targets);
    health_checker.start();

    std::cout << "🔄 [MAIN ] StudySync 메인서버 시작 완료 | Ctrl+C 종료" << std::endl;

    while (!g_should_exit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "🔄 [MAIN ] 서버 종료 중..." << std::endl;
    health_checker.stop();
    tcp_listener.stop();
    db_pool.shutdown();
    event_bus.stop();
    return 0;
}
