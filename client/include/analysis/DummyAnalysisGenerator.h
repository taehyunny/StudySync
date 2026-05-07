#pragma once

#include "event/EventQueue.h"
#include "event/EventShadowBuffer.h"
#include "event/PostureEventDetector.h"
#include "model/AnalysisResultBuffer.h"

#include <atomic>
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

private:
    void run(int interval_ms);

    AnalysisResultBuffer& result_buffer_;
    EventShadowBuffer&    shadow_buffer_;
    EventQueue&           event_queue_;
    PostureEventDetector  detector_;

    std::atomic_bool running_{ false };
    std::thread worker_;
};
