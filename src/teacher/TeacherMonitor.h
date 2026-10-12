#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <QMutex>
#include <QWaitCondition>
#include <QThread>

#include "TeacherConfig.h"
#include "TeacherPomodoro.h"
#include "TeacherRules.h"
#include "TeacherStats.h"
#include "TeacherTracker.h"

namespace pic2pet {

class ScreenGrabber;
class CameraReader;
class TeacherJudge;

/** 每次判定后的状态快照（广播给设置页 / 菜单用） */
struct TeacherStatus {
    TeacherState state = TeacherState::Unsure;
    QString reason;
    double elapsedS = 0.0;
    double nextIntervalS = 0.0;
    bool personPresent = false;    // 摄像头关了 / 读不到画面时看 hasPersonInfo
    bool hasPersonInfo = false;
    bool usedApi = false;
    bool pomodoroEnabled = false;
    TeacherPhase phase = TeacherPhase::Focus;
    double pomodoroRemainingS = 0.0;
    double pomodoroTotalS = 0.0;
    int pomodoroRounds = 0;
    bool inBreak = false;
    // 统计（与 stats_ 同步的副本，避免界面直接摸线程内对象）
    int screenshots = 0;
    int cameraShots = 0;
    int localHits = 0;
    int apiCalls = 0;
    int apiFailed = 0;
    int apiSkipped = 0;
    int focusTicks = 0;
    int distractedTicks = 0;
    int unsureTicks = 0;
    int alerts = 0;
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
};

/** 会话结束时的报告 */
struct TeacherReport {
    QString goal;
    TeacherStats stats;
};

// 信号跨线程（工作线程 → 主线程）走 queued 连接，参数类型必须注册元类型。
// 结构体只含可 memcpy 的成员（枚举成员不需要单独注册）。
Q_DECLARE_METATYPE(pic2pet::TeacherStatus)
Q_DECLARE_METATYPE(pic2pet::TeacherReport)

/**
 * AI 教官监控主循环：串联 L0 → L1 → L2。
 *
 * 本身就是工作线程（QThread 子类）：截图、摄像头、网络都是阻塞操作，
 * 放主线程会把界面卡死。平台资源（GDI / MF / COM）都在 run() 里出生、
 * 在 run() 里销毁——MF 的 COM 实例有线程亲和性，不能跨线程构造析构。
 *
 * 与参考实现的对应关系：
 *   - tick()      → monitor.py 的 tick()（L0 → 按需 L1 → L2 → 告警）
 *   - runBreak()  → 休息阶段只倒计时，**完全不抓拍**（摄像头指示灯都灭着）
 *   - goal 运行中可改，只改内存值，不动线程、不重置统计
 *   - **图片不落盘、不记录 key / 请求体**，日志只记字节数
 */
class TeacherMonitor : public QThread {
    Q_OBJECT

public:
    explicit TeacherMonitor(const TeacherConfig& config, QObject* parent = nullptr);
    ~TeacherMonitor() override;

    /** 启动监控（开新线程） */
    void start();
    /** 请求停止。线程安全，任意时刻可调；会唤醒等待中的 sleep */
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /** 运行中修改学习目标：下一次 tick 的提示词就用新目标 */
    void setGoal(const QString& goal);

    const TeacherConfig& config() const { return cfg_; }

signals:
    void started(const QString& goal, bool hasApiKey, bool pomodoro);
    /** 每次判定后广播（设置页状态卡 / 托盘 tooltip 用） */
    void statusChanged(const pic2pet::TeacherStatus& status);
    /** 分心告警（由主线程弹托盘通知 + 提示音） */
    void alert(const QString& reason, const QString& goal);
    /** 课堂模式阶段切换 / 倒计时 */
    void pomodoroChanged(const pic2pet::TeacherPomodoroState& state);
    void pomodoroBreak(const QString& message);
    void pomodoroFocus(const QString& message);
    /** 一行运行日志（界面可展示；qInfo 之外的第二通道） */
    void logLine(const QString& line);
    /** 会话结束（线程退出前发最后一次） */
    void finished(const pic2pet::TeacherReport& report);

protected:
    void run() override;

private:
    TeacherDecision tickOnce();
    void enterBreakPhase();
    void runBreak();
    void shutdown();
    void waitSleep(double seconds);
    QString currentGoal() const;
    void emitStatus(const TeacherDecision& d, const TeacherSignals& sig);

    TeacherConfig cfg_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    QMutex waitMutex_;
    QWaitCondition waitCond_;

    // goal 运行中可改：工作线程读、主线程写，加锁保护
    mutable QMutex goalMutex_;
    QString goal_;

    // ---- 以下都在 run() 里创建 / 销毁（线程亲和性）----
    std::unique_ptr<ScreenGrabber> grabber_;
    std::unique_ptr<CameraReader> camera_;
    std::unique_ptr<TeacherRules> rules_;
    std::unique_ptr<TeacherJudge> judge_;
    std::unique_ptr<TeacherTracker> tracker_;
    std::unique_ptr<TeacherPomodoro> pomodoro_;
    std::unique_ptr<TeacherStats> stats_;
    bool warnedCameraMissing_ = false;
};

} // namespace pic2pet
