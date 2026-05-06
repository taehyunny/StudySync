// ============================================================================
// main.cpp — 공장 품질검사 메인 운영 서버 진입점
// ============================================================================
// 목적:
//   EventBus를 중심으로 모든 컴포넌트를 생성하고 핸들러를 등록한 뒤,
//   TCP 리스너와 헬스체커를 기동한다.
//
// 아키텍처 요약:
//   EventBus (Pub/Sub 허브)
//     ├─ TcpListener      : AI 추론 서버 패킷 수신 → PACKET_RECEIVED 발행
//     ├─ Router            : PACKET_RECEIVED 구독 → 프로토콜별 이벤트 재발행
//     ├─ StationHandler    : 검사 이벤트 구독 → 검증 후 DB/이미지 저장 요청
//     ├─ DbManager         : DB_WRITE_REQUESTED 구독 → MySQL 기록
//     ├─ ImageStorage      : IMAGE_SAVE_REQUESTED 구독 → NG 이미지 디스크 저장
//     ├─ AckSender         : ACK_SEND_REQUESTED 구독 → 추론서버에 응답
//     ├─ GuiNotifier       : GUI_PUSH_REQUESTED 구독 → MFC 클라이언트 푸시
//     ├─ GuiTcpListener    : MFC 클라이언트 TCP 접속 + DB 조회 요청 처리
//     └─ HealthChecker     : 추론/학습 서버 주기적 생존 확인
//
// 종료 흐름:
//   SIGINT/SIGTERM → g_should_exit 플래그 → 메인 루프 탈출 → 역순 정리
// ============================================================================

#include "core/event_bus.h"
#include "core/tcp_listener.h"
#include "core/config.h"
#include "handler/router.h"
#include "handler/station_handler.h"
#include "handler/ack_sender.h"
#include "handler/train_handler.h"
#include "storage/connection_pool.h"
#include "service/inspection_service.h"
#include "service/train_service.h"
#include "session/gui_notifier.h"
#include "session/gui_tcp_listener.h"
#include "session/gui_service.h"
#include "monitor/health_checker.h"
#include "Protocol.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
// SIGINT/SIGTERM 시 true로 설정 → 메인 루프 종료
std::atomic<bool> g_should_exit{false};
void on_signal(int) { g_should_exit.store(true); }
} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    using namespace factory;

    // ────────────────────────────────────────────────────────────
    // 0) 설정 파일 로드 (명령줄 인자 > 환경변수 > 기본값)
    //    실행 예: ./factory_main_server ../../config/config.json
    //    또는:    CONFIG_PATH=/path/config.json ./factory_main_server
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

    // 자주 사용되는 값 캐싱
    auto& cfg = Config::instance();
    uint16_t    ai_port       = static_cast<uint16_t>(cfg.get_int("network.main_server_ai_port", 9000));
    uint16_t    gui_port      = static_cast<uint16_t>(cfg.get_int("network.main_server_gui_port", 9010));
    uint16_t    train_port    = static_cast<uint16_t>(cfg.get_int("network.training_server_port", 9100));
    std::string db_host       = cfg.get_str("database.host",     "127.0.0.1");
    std::string db_user       = cfg.get_str("database.user",     "factorymanager");
    std::string db_password   = cfg.get_str("database.password", "1234");
    std::string db_schema     = cfg.get_str("database.schema",   "Factory");
    int         db_pool_size  = cfg.get_int("database.pool_size", 4);
    std::string image_root    = cfg.get_str("storage.image_root", "./storage");
    int         worker_count  = cfg.get_int("limits.event_queue_max", 10000) > 0 ? 4 : 4;

    // 학습서버 주소: network.training_server_host (메인과 다른 PC에 배치 가능)
    // 환경변수 TRAIN_HOST가 있으면 config보다 우선 (배포 유연성)
    std::string train_host = cfg.get_str("network.training_server_host", "10.10.10.120");
    if (const char* env = std::getenv("TRAIN_HOST")) train_host = env;

    // 1) EventBus 생성 및 시작 (워커 4개 — DB/이미지/ACK/GUI 병렬 처리)
    EventBus event_bus(worker_count);
    event_bus.start();

    // 2) 컴포넌트 생성 + 핸들러 등록
    Router router(event_bus);
    router.register_handlers();

    // 커넥션 풀 생성 — Service, DAO, GuiTcpListener가 공유
    ConnectionPool db_pool(db_host, db_user, db_password, db_schema, 3306, db_pool_size);
    if (!db_pool.init()) {
        std::cerr << "DB 커넥션 풀 초기화 실패" << std::endl;
        return 1;
    }

    // Service 레이어 — v0.12.0 부터 InspectionService 는 EventBus 에 직접 구독
    // (StationHandler 가 validate 후 ACK 발행 → INSPECTION_VALIDATED 로 위임)
    InspectionService inspection_service(event_bus, db_pool, image_root);
    inspection_service.register_handlers();

    TrainService train_service(db_pool);

    // Handler → Service 주입 (validate_only 호출용)
    Station1Handler station1_handler(event_bus, inspection_service);
    station1_handler.register_handlers();

    Station2Handler station2_handler(event_bus, inspection_service);
    station2_handler.register_handlers();

    TrainHandler train_handler(event_bus, train_service);
    train_handler.register_handlers();

    GuiNotifier gui_notifier(event_bus);
    gui_notifier.register_handlers();

    AckSender ack_sender(event_bus);
    ack_sender.register_handlers();

    // 3) TCP 리스너 시작 (추론 서버로부터 패킷 수신)
    TcpListener tcp_listener(event_bus, ai_port);
    tcp_listener.start();

    // 4) GUI TCP 리스너 시작 (TCP수신 → GuiRouter → GuiService → DAO)
    GuiService gui_service(db_pool, train_host, train_port);
    GuiRouter gui_router(gui_service);
    GuiTcpListener gui_tcp_listener(event_bus, gui_port, gui_router);
    gui_tcp_listener.start();

    // 5) 헬스체크 시작 — config.json에서 타겟 목록 로드
    std::vector<HealthTarget> targets;
    for (const auto& tc : cfg.get_health_targets()) {
        targets.push_back({tc.name, tc.ip, static_cast<uint16_t>(tc.port)});
    }
    HealthChecker health_checker(event_bus, targets);
    health_checker.start();

    std::cout << "🔄 [MAIN ] 서버 시작 완료 | Ctrl+C 종료" << std::endl;

    // 메인 스레드는 종료 신호가 올 때까지 200ms 간격으로 폴링
    while (!g_should_exit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 종료 순서: 외부 입출력(리스너) → 저장소(DB) → 내부 버스
    // 리스너를 먼저 닫아 새 이벤트 유입을 차단한 뒤, 버스를 마지막에 정리
    std::cout << "🔄 [MAIN ] 서버 종료 중..." << std::endl;
    health_checker.stop();
    gui_tcp_listener.stop();
    tcp_listener.stop();
    db_pool.shutdown();
    event_bus.stop();
    return 0;
}
