#pragma once
// ============================================================================
// event_bus.h — 이벤트 버스 (워커 풀 기반 Pub/Sub 메시지 브로커)
// ============================================================================
// 목적:
//   서버 내부 컴포넌트 간 느슨한 결합을 위한 이벤트 시스템.
//
// 설계 결정:
//   - 워커 풀(N개 스레드)이 큐에서 이벤트를 꺼내 병렬 처리한다.
//     → 다중 접속 시 DB INSERT, 이미지 저장 등이 동시 수행되어 병목 제거.
//   - 동일 이벤트의 핸들러들은 한 워커 안에서 순차 호출된다.
//     → 같은 이벤트 내 핸들러 순서는 보장.
//   - 서로 다른 이벤트는 서로 다른 워커에서 병렬 처리될 수 있다.
//     → 핸들러 내부에서 공유 자원 접근 시 동기화 필요 (DAO는 ConnectionPool로 보호됨).
//   - 페이로드는 std::any로 타입 소거.
//
// 사용 흐름:
//   1. EventBus 생성 (워커 수 지정, 기본 4) → start()
//   2. subscribe(EventType, handler)
//   3. publish(EventType, payload) → 큐 적재 → 워커 풀이 병렬 처리
//   4. stop() → 모든 워커 종료 대기
// ============================================================================

#include "core/event_types.h"

#include <any>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factory {

class EventBus {
public:
    using Handler = std::function<void(const std::any&)>;

    /// @param worker_count 워커 스레드 수 (기본 4)
    /// @param max_queue_size 큐 최대 크기 (기본 10000, 초과 시 drop)
    explicit EventBus(int worker_count = 4, std::size_t max_queue_size = 10000);
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// 워커 풀 기동
    void start();
    /// 모든 워커에 종료 신호 → join
    void stop();

    /// 이벤트 구독 (동일 이벤트에 여러 핸들러 등록 가능)
    void subscribe(EventType type, Handler handler);

    /// 이벤트 발행 (논블로킹, 큐 적재 후 즉시 반환)
    void publish(EventType type, std::any payload);

private:
    void worker_loop();

    struct Event {
        EventType type;
        std::any  payload;
    };

    // 핸들러 맵
    std::unordered_map<EventType, std::vector<Handler>> handler_map_;
    std::mutex                                          handler_mutex_;

    // 이벤트 큐
    std::queue<Event>                                   event_queue_;
    std::mutex                                          queue_mutex_;
    std::condition_variable                             queue_cv_;

    // 워커 풀
    std::vector<std::thread>                            workers_;
    int                                                 worker_count_;
    std::size_t                                         max_queue_size_;
    std::atomic<bool>                                   is_running_;
};

} // namespace factory
