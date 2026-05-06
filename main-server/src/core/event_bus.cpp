// ============================================================================
// event_bus.cpp — 인-프로세스 EventBus (Worker Pool 기반)
// ============================================================================
// 패턴:
//   Publish/Subscribe + Thread Pool. 생산자(TcpListener, Service 등) 는
//   publish() 로 큐에 이벤트를 적재하고 즉시 반환(논블로킹). 워커 스레드 N개가
//   큐에서 경쟁적으로 이벤트를 꺼내 구독자(Handler) 들을 호출한다.
//
// 왜 이벤트 버스인가:
//   - 각 핸들러(DAO, AckSender, GuiNotifier 등) 를 약결합시켜 테스트/교체 용이.
//   - NG 패킷 1건이 DB/이미지/ACK/GUI 푸시로 팬아웃될 때 병렬 처리 가능.
//   - TcpListener 스레드가 블로킹되지 않아 수신 throughput 저하 없음.
//
// 큐 오버플로우 정책:
//   max_queue_size (config.limits.event_queue_max, 기본 10000) 초과 시 드롭 + 로그.
//   백프레셔를 호출자에 반환하지 않는 이유: 드롭은 drop-tail 단일 방식이라
//   복잡도 낮고, 정상 운영에서는 이 상한에 거의 도달하지 않음.
//
// 핸들러 실행 순서 보장:
//   같은 EventType 에 여러 핸들러 구독 시 "구독 등록 순서대로" 순차 호출.
//   다른 이벤트는 서로 다른 워커가 병렬 처리.
//
// 예외 격리:
//   개별 핸들러 예외는 catch 하여 로그만 남기고 다음 핸들러 계속 실행 →
//   한 구독자가 예외를 던져도 다른 구독자가 멈추지 않음.
// ============================================================================

#include "core/event_bus.h"
#include "core/logger.h"

namespace factory {

EventBus::EventBus(int worker_count, std::size_t max_queue_size)
    : worker_count_(worker_count),
      max_queue_size_(max_queue_size),
      is_running_(false) {
}

EventBus::~EventBus() {
    stop();
}

// ---------------------------------------------------------------------------
// start — 워커 스레드 N개 생성. 중복 start 방지(atomic exchange).
// main.cpp 에서 모든 핸들러 구독 완료 후 호출해야 — 워커가 "빈 핸들러맵" 으로
// 돌면 초기 이벤트가 버려질 수 있음.
// ---------------------------------------------------------------------------
void EventBus::start() {
    if (is_running_.exchange(true)) return;

    workers_.reserve(worker_count_);
    for (int i = 0; i < worker_count_; ++i) {
        workers_.emplace_back(&EventBus::worker_loop, this);
    }
    log_main("EventBus 시작 | 워커=%d개", worker_count_);
}

// ---------------------------------------------------------------------------
// stop — 우아한 종료
//   1) is_running_ = false
//   2) 모든 워커에게 notify_all — cv.wait 대기 중인 스레드 깨움
//   3) 각 워커가 "큐가 비어있으면" 루프 탈출 → 남은 이벤트는 드레인 후 종료
//   4) join 대기
// ---------------------------------------------------------------------------
void EventBus::stop() {
    if (!is_running_.exchange(false)) return;

    queue_cv_.notify_all();

    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
    log_main("EventBus 종료 | 워커 %d개 정리 완료", worker_count_);
}

// ---------------------------------------------------------------------------
// subscribe — 특정 EventType 에 핸들러 추가
// 멀티 구독 가능 (같은 타입에 여러 핸들러).
// 현재는 등록 해제 API 없음 — 애플리케이션 수명 동안 유지되는 핸들러만 존재.
// ---------------------------------------------------------------------------
void EventBus::subscribe(EventType type, Handler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handler_map_[type].push_back(std::move(handler));
}

// ---------------------------------------------------------------------------
// publish — 이벤트를 큐에 적재하고 워커 1명 깨움
// std::any 로 payload 를 타입 이레이저로 감싸 모든 종류의 이벤트를 단일 큐에
// 담을 수 있음. 핸들러가 std::any_cast 로 복원.
// 큐 포화 시: drop-tail (오래된 것을 남기고 신규 드롭) — 로그만 남기고 return.
// ---------------------------------------------------------------------------
void EventBus::publish(EventType type, std::any payload) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.size() >= max_queue_size_) {
            log_err_main("EventBus 큐 포화 | size=%zu — 이벤트 드롭", event_queue_.size());
            return;
        }
        event_queue_.push(Event{type, std::move(payload)});
    }
    queue_cv_.notify_one();  // 잠자고 있는 워커 하나 깨움
}

// ---------------------------------------------------------------------------
// worker_loop — 각 워커 스레드의 메인 루프
//
// 수명:
//   1) queue_cv_ 로 이벤트 대기 (또는 종료 신호)
//   2) 큐에서 꺼내 → handler_map_ 에서 구독자 복사 → mutex 해제 → 순차 호출
//   3) 예외 격리: 한 핸들러 예외가 다른 핸들러/다음 이벤트를 막지 않음
//
// 락 분리 전략:
//   queue_mutex_  : 이벤트 큐 전용
//   handler_mutex_: 구독자 맵 전용
//   → 핸들러 복사/실행 중 publish 경쟁 없음 (짧은 임계구간 유지)
// ---------------------------------------------------------------------------
void EventBus::worker_loop() {
    while (true) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !event_queue_.empty() || !is_running_.load();
            });

            // 종료 신호 수신 + 큐도 비었다면 루프 탈출
            if (!is_running_.load() && event_queue_.empty()) break;
            if (event_queue_.empty()) continue;   // spurious wakeup 방어

            event = std::move(event_queue_.front());
            event_queue_.pop();
        }

        // 핸들러 목록을 락 밖으로 복사 — 실행 중에 다른 스레드가 subscribe 가능
        std::vector<Handler> handlers_snapshot;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto it = handler_map_.find(event.type);
            if (it != handler_map_.end()) {
                handlers_snapshot = it->second;
            }
        }

        // 구독자들을 순차 호출. 각 핸들러의 예외는 개별적으로 격리.
        for (auto& handler : handlers_snapshot) {
            try {
                handler(event.payload);
            } catch (const std::exception& e) {
                log_err_main("이벤트 핸들러 예외 | %s", e.what());
            } catch (...) {
                log_err_main("이벤트 핸들러 알수없는 예외");
            }
        }
    }
}

} // namespace factory
