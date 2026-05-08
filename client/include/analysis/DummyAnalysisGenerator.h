#pragma once

#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "event/PostureEventDetector.h"
#include "model/AnalysisResultBuffer.h"

#include <atomic>
#include <functional>
#include <thread>

// ============================================================================
// DummyAnalysisGenerator — AI 서버 없이 더미 분석결과를 주기적으로 생성
// ============================================================================
// AiTcpClient 대신 활성화하여 전체 파이프라인을 로컬에서 테스트한다.
//   result_buffer_.update()   → RenderThread HUD 표시
//   detector_.feed()          → PostureEvent → EventQueue → EventUploadThread
//
// 시나리오 사이클 (약 30초 주기):
//   0~9s  : focus   (정상 집중)
//   10~14s: drowsy  (졸음)
//   15~19s: bad_posture (목 각도 초과)
//   20~24s: absent  (자리 비움)
//   25~29s: focus   (복귀)
// ============================================================================
class DummyAnalysisGenerator {
public:
    DummyAnalysisGenerator(AnalysisResultBuffer& result_buffer,
                           EventShadowBuffer& shadow_buffer,
                           EventQueue& event_queue);
    ~DummyAnalysisGenerator();

    // interval_ms: result 생성 주기 (기본 200ms → 약 5fps)
    void start(int interval_ms = 200);
    void stop();

    // AnalysisResult 생성마다 호출할 콜백 (AlertManager 배선용, start() 전에 설정)
    using ResultCallback = std::function<void(const AnalysisResult&)>;
    void set_result_callback(ResultCallback cb) { result_callback_ = std::move(cb); }

    // 캘리브레이션 완료 후 자세 임계값 갱신 (스레드 안전: atomic setter 위임)
    void set_neck_threshold(double deg) { detector_.set_neck_threshold(deg); }
    void set_ear_threshold(float val)   { detector_.set_ear_threshold(val);  }

private:
    void run(int interval_ms);

    AnalysisResultBuffer& result_buffer_;
    EventShadowBuffer&    shadow_buffer_;
    EventQueue&           event_queue_;
    PostureEventDetector  detector_;
    ResultCallback        result_callback_;

    std::atomic_bool running_{ false };
    std::thread worker_;
};
