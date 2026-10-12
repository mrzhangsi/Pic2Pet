#pragma once

#include <QElapsedTimer>
#include <QMetaType>     // Q_DECLARE_METATYPE
#include <QString>

namespace pic2pet {

/** 课堂模式阶段 */
enum class TeacherPhase {
    Focus,
    Break,
};

/** 课堂模式运行时状态快照（纯数据，便于界面展示） */
struct TeacherPomodoroState {
    TeacherPhase phase = TeacherPhase::Focus;
    double remainingS = 0.0;   // 本阶段剩余秒数
    double totalS = 0.0;       // 本阶段总时长
    int roundsDone = 0;        // 已完成的专注轮次
    double breakTotalS = 0.0;  // 累计休息秒数

    double progress() const;   // 0.0~1.0
    QString remainingFmt() const;   // "04:59" / "1:05:00"
    QString phaseLabel() const;     // "专注中" / "休息中"
};

// 课堂模式状态跨线程广播（queued 连接），注册元类型
Q_DECLARE_METATYPE(pic2pet::TeacherPomodoroState)

/**
 * 课堂模式计时器（番茄钟）。
 *
 * **不负责睡觉**，只回答两个问题：现在该专注还是休息？还剩多久？
 * 实际等待由 TeacherMonitor 的可中断 sleep 完成，所以用户随时能停止。
 * 休息期间完全不截图不拍照——省电、不打扰，摄像头指示灯也灭着。
 */
class TeacherPomodoro {
public:
    TeacherPomodoro(double focusMinutes, double breakMinutes, bool enabled);

    void updateElapsed();          // 累计本阶段已过秒数（每次 tick 前调一次）
    TeacherPomodoroState state() const;
    bool phaseExpired() const;
    /** 时间到了，推进到下一阶段，返回新阶段 */
    TeacherPhase advance();
    bool inBreak() const { return phase_ == TeacherPhase::Break; }
    int roundsDone() const { return roundsDone_; }
    void addBreakTime(double seconds);
    QString describe() const;
    bool enabled() const { return enabled_; }

private:
    double remaining(double total) const;

    bool enabled_;
    double focusS_;
    double breakS_;
    QElapsedTimer clock_;
    double phaseStartedMs_ = 0.0;
    double elapsedInPhase_ = 0.0;
    TeacherPhase phase_ = TeacherPhase::Focus;
    int roundsDone_ = 0;
    double breakTotalS_ = 0.0;
};

} // namespace pic2pet
