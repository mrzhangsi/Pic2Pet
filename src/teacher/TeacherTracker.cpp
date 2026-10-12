#include "TeacherTracker.h"

namespace pic2pet {

TeacherTracker::TeacherTracker(const TeacherJudgeCfg& cfg)
    : cfg_(cfg), intervalS_(cfg.intervalMinS) {
    clock_.start();
    startedAtMs_ = 0.0;
}

double TeacherTracker::elapsedS() const {
    return clock_.elapsed() / 1000.0;
}

void TeacherTracker::growInterval() {
    // 状态好 → 逐步拉长采样间隔，省调用次数。用掉一次成长机会。
    if (focusStreak_ < cfg_.intervalGrowAfter) return;
    if (intervalS_ < cfg_.intervalMaxS) {
        intervalS_ = qMin(cfg_.intervalMaxS, intervalS_ * 1.6);
    }
    focusStreak_ = 0;
}

void TeacherTracker::shrinkInterval() {
    // 可疑 → 立刻回落到最短间隔，尽快确认。
    intervalS_ = cfg_.intervalMinS;
    focusStreak_ = 0;
}

TeacherDecision TeacherTracker::update(TeacherVerdict l0, const QString& reason,
                                       const bool* modelStudying, bool usedApi) {
    TeacherDecision d;
    d.fromL0 = l0;
    d.reason = reason;
    d.usedApi = usedApi;

    ++samples_;
    if (usedApi) ++apiCalls_;

    // 1) 汇总本 tick 的是否在学习。L0 只定"不在"；其余听模型的。
    //    modelStudying == nullptr 表示本 tick 没有模型结论（L0 定案 / 调用失败）。
    const bool* studying = nullptr;
    if (l0 == TeacherVerdict::Focus) {
        static const bool kTrue = true;
        studying = &kTrue;
    } else if (l0 == TeacherVerdict::Distract) {
        static const bool kFalse = false;
        studying = &kFalse;
    } else {
        studying = modelStudying;
    }

    if (studying) {
        const bool s = *studying;
        ewmaFocus_ = cfg_.focusEwmaAlpha * (s ? 1.0 : 0.0) + (1.0 - cfg_.focusEwmaAlpha) * ewmaFocus_;
        if (!s) {
            ++strikes_;
            focusStreak_ = 0;
        } else {
            strikes_ = 0;
            ++focusStreak_;
        }
        if (ewmaFocus_ >= cfg_.focusHealthyRatio) {
            state_ = TeacherState::Focusing;
        } else if (ewmaFocus_ <= cfg_.focusBadRatio) {
            state_ = TeacherState::Distracted;
        } else {
            state_ = TeacherState::Unsure;
        }
    } else {
        state_ = TeacherState::Unsure;
    }

    // 2) 判定是否该告警
    d.shouldAlert = shouldAlert(state_, l0, studying);

    // 3) 调整下次间隔
    if (state_ == TeacherState::Focusing) {
        growInterval();
    } else {
        shrinkInterval();
    }

    d.state = state_;
    d.nextIntervalS = intervalS_;
    return d;
}

bool TeacherTracker::shouldAlert(TeacherState state, TeacherVerdict l0, const bool* studying) {
    if (state == TeacherState::Focusing) return false;

    const double now = clock_.elapsed() / 1000.0;
    // 宽限期：启动初期不打扰
    if (now - startedAtMs_ < cfg_.gracePeriodS) return false;
    // 冷却：避免刷屏
    if (lastAlertAtMs_ > -1e17 && now - lastAlertAtMs_ < cfg_.alertCooldownS) return false;

    // L0 硬规则：确定性高，立即告警
    if (l0 == TeacherVerdict::Distract) {
        lastAlertAtMs_ = now;
        ++alertCount_;
        return true;
    }

    // L1 模型判定：需连续 N 次。strikes 消费掉，避免冷却结束后立刻再报。
    if (studying && !*studying && strikes_ >= cfg_.strikesToWarn) {
        strikes_ = 0;
        lastAlertAtMs_ = now;
        ++alertCount_;
        return true;
    }
    return false;
}

} // namespace pic2pet
