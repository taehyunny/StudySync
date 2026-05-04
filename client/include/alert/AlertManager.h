#pragma once

#include "alert/AlertQueue.h"
#include "model/AnalysisResult.h"

#include <cstdint>

class AlertManager {
public:
    explicit AlertManager(AlertQueue& alert_queue);

    void feed_local_analysis(const AnalysisResult& result);
    void feed_server_alert(const Alert& alert);
    void reset();

private:
    void push_alert(AlertType type, AlertTarget target, std::uint64_t timestamp_ms, const char* title, const char* message);

    AlertQueue& alert_queue_;
    int bad_posture_streak_ = 0;
    int drowsy_streak_ = 0;
    int focus_low_streak_ = 0;
    bool posture_alert_cooldown_ = false;
    bool drowsy_alert_cooldown_ = false;
};

