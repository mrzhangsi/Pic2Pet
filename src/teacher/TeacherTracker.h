#pragma once

#include <QElapsedTimer>

#include "TeacherConfig.h"
#include "TeacherRules.h"

namespace pic2pet {

/** L2 聚合后的学习状态 */
enum class TeacherState {
    Focusing,
    Unsure,
    Distracted,
};

/** 单次 tick 的综合结论 */
struct TeacherDecision {
    TeacherState state = TeacherState::Unsure;
    QString reason;
    bool shouldAlert = false;
    double nextIntervalS = 60.0;
    bool usedApi = false;
    TeacherVerdict fromL0 = TeacherVerdict::Unknown;
};

/**
 * L2 状态机：抗抖动 + 自适应采样间隔 + 告警触发。
 *
 * 单帧误报是必然的（回条消息、切个窗口都会被拍到），所以：
 *   - L0 硬规则分心 → 立即告警（确定性高）
 *   - L1 模型判定分心 → 需连续 N 次才告警
 *   - 启动有宽限期，期间不打扰
 *   - 告警有冷却，避免刷屏
 *   - 持续在学 → 采样间隔 45s → 90s → 180s → 300s 封顶；一旦可疑立刻回落
 */
class TeacherTracker {
public:
    explicit TeacherTracker(const TeacherJudgeCfg& cfg);

    /** 吃进本 tick 的信号，产出综合决策 */
    TeacherDecision update(TeacherVerdict l0, const QString& reason,
                           const bool* modelStudying, bool usedApi);

    TeacherState state() const { return state_; }
    double ewmaFocus() const { return ewmaFocus_; }
    double intervalS() const { return intervalS_; }
    int alertCount() const { return alertCount_; }
    int samples() const { return samples_; }
    int apiCalls() const { return apiCalls_; }
    double elapsedS() const;

private:
    bool shouldAlert(TeacherState state, TeacherVerdict l0, const bool* studying);
    void growInterval();
    void shrinkInterval();

    TeacherJudgeCfg cfg_;
    QElapsedTimer clock_;
    double startedAtMs_ = 0.0;
    int strikes_ = 0;
    int focusStreak_ = 0;
    double lastAlertAtMs_ = -1e18;
    double intervalS_;
    double ewmaFocus_ = 1.0;
    int alertCount_ = 0;
    int samples_ = 0;
    int apiCalls_ = 0;
    TeacherState state_ = TeacherState::Unsure;
};

} // namespace pic2pet
