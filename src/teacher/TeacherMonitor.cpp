#include "TeacherMonitor.h"

#include "TeacherCamera.h"
#include "TeacherCapture.h"
#include "TeacherJudge.h"

#include <QElapsedTimer>

namespace pic2pet {

TeacherMonitor::TeacherMonitor(const TeacherConfig& config, QObject* parent)
    : QThread(parent), cfg_(config), goal_(config.goal) {}

TeacherMonitor::~TeacherMonitor() {
    // 正常路径：run() 结束即线程退出。防御性等待，避免析构时线程还在跑。
    if (isRunning()) {
        stop();
        wait(8000);
    }
}

void TeacherMonitor::start() {
    stopRequested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    QThread::start();
}

void TeacherMonitor::stop() {
    stopRequested_.store(true, std::memory_order_release);
    QMutexLocker locker(&waitMutex_);
    waitCond_.wakeAll();
}

void TeacherMonitor::setGoal(const QString& goal) {
    QString g = goal.trimmed();
    if (g.isEmpty()) return;
    QMutexLocker locker(&goalMutex_);
    if (g == goal_) return;
    goal_ = g;
}

QString TeacherMonitor::currentGoal() const {
    QMutexLocker locker(&goalMutex_);
    return goal_;
}

void TeacherMonitor::waitSleep(double seconds) {
    // 可中断的 sleep：stop() 会唤醒，不用傻等整个间隔
    QMutexLocker locker(&waitMutex_);
    if (!stopRequested_.load(std::memory_order_acquire)) {
        waitCond_.wait(&waitMutex_, quint64(qMax(0.0, seconds) * 1000.0));
    }
}

void TeacherMonitor::run() {
    // ---- 平台资源必须在监控线程里出生（MF 的 COM 实例有线程亲和性）----
    grabber_ = std::make_unique<ScreenGrabber>();
    if (cfg_.monitorCamera) {
        camera_ = std::make_unique<CameraReader>(cfg_.capture.cameraIndex);
    }
    rules_ = std::make_unique<TeacherRules>(cfg_.rules, cfg_.capture);
    judge_ = std::make_unique<TeacherJudge>(cfg_.api);
    tracker_ = std::make_unique<TeacherTracker>(cfg_.judge);
    pomodoro_ = std::make_unique<TeacherPomodoro>(cfg_.pomodoro.focusMinutes,
                                                  cfg_.pomodoro.breakMinutes,
                                                  cfg_.pomodoro.enabled);
    stats_ = std::make_unique<TeacherStats>();

    const bool hasKey = !cfg_.api.apiKey.isEmpty();
    const QString goal = currentGoal();
    qInfo().noquote() << QStringLiteral(
        "[teacher] 监控启动 | 目标=%1 | api=%2 | 课堂模式=%3 | 间隔 %4~%5s")
        .arg(goal, hasKey ? QStringLiteral("有") : QStringLiteral("无"),
             cfg_.pomodoro.enabled ? QStringLiteral("开") : QStringLiteral("关"))
        .arg(int(cfg_.judge.intervalMinS)).arg(int(cfg_.judge.intervalMaxS));
    if (!hasKey) {
        qInfo().noquote() << QStringLiteral(
            "[teacher] 未配置 API key，只走本地规则（锁屏 / 人离开），模型判定跳过");
    }
    emit started(goal, hasKey, cfg_.pomodoro.enabled);

    TeacherDecision decision;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        // ---- 休息阶段：不抓拍、不判定，只倒计时
        if (pomodoro_->enabled() && pomodoro_->inBreak()) {
            runBreak();
            continue;
        }

        decision = tickOnce();

        // ---- 专注时长到了？切换到休息
        if (pomodoro_->enabled()) {
            pomodoro_->updateElapsed();
            if (pomodoro_->phaseExpired()) {
                pomodoro_->advance();
                emit pomodoroChanged(pomodoro_->state());
                enterBreakPhase();
                continue;
            }
        }

        if (stopRequested_.load(std::memory_order_acquire)) break;
        waitSleep(decision.nextIntervalS);
    }

    shutdown();

    // 先在本线程销毁平台资源（MF / COM / GDI），再退出
    camera_.reset();
    grabber_.reset();

    running_.store(false, std::memory_order_release);
}

TeacherDecision TeacherMonitor::tickOnce() {
    const TeacherCaptureCfg& cap = cfg_.capture;

    ++stats_->screenshots;
    const QImage screen = grabber_->grab();

    QImage camera;
    if (camera_) {
        camera = camera_->read();
        if (!camera.isNull()) {
            ++stats_->cameraShots;
        } else {
            ++stats_->cameraMiss;
            if (!warnedCameraMissing_) {
                warnedCameraMissing_ = true;
                qInfo().noquote() << QStringLiteral(
                    "[teacher] 摄像头不可用（被占用或无设备），将仅凭屏幕判定");
            }
        }
    }

    // ---- L0 本地规则
    const QImage* camPtr = camera.isNull() ? nullptr : &camera;
    const TeacherSignals sig = rules_->evaluate(screen, camPtr);

    // ---- L1 模型判定（仅在 L0 拿不准时才花钱）
    bool usedApi = false;
    bool modelOk = false;
    bool modelStudying = false;
    QString reason = sig.reason;

    if (sig.verdict == TeacherVerdict::Suspect || sig.verdict == TeacherVerdict::Unknown) {
        if (cfg_.api.apiKey.isEmpty()) {
            ++stats_->apiSkipped;
        } else {
            // 摄像头画面是否上传：关掉仍本地做人脸/肤色检测，不花钱、不外传
            const QImage* uploadCam = (cfg_.uploadCamera && camPtr) ? camPtr : nullptr;
            const QByteArray jpeg = teacherComposeJpeg(screen, uploadCam, cap);
            stats_->imageBytes += jpeg.size();

            const TeacherJudgeResult r =
                judge_->judge(jpeg, currentGoal(), [this] {
                    return stopRequested_.load(std::memory_order_acquire);
                });
            if (r.ok) {
                ++stats_->apiCalls;
                usedApi = true;
                modelOk = true;
                modelStudying = r.studying;
                if (!r.comment.isEmpty()) reason = r.comment;
                stats_->promptTokens += r.promptTokens;
                stats_->completionTokens += r.completionTokens;
            } else if (r.error == QLatin1String("no_api_key")) {
                ++stats_->apiSkipped;
            } else {
                // 调用失败时保守处理：不动状态，也不打扰用户
                ++stats_->apiFailed;
                if (r.error == QLatin1String("budget_exceeded")) {
                    reason += QStringLiteral("（已达调用预算，本次不再调用模型）");
                } else {
                    reason += QStringLiteral("（模型不可用：%1）").arg(r.error.left(30));
                }
            }
        }
    } else {
        // L0 直接定案，完全没有花钱
        ++stats_->localHits;
    }

    // ---- L2 状态机。modelStudying 只在模型真的给出结论时才有效
    const bool* studyingPtr = modelOk ? &modelStudying : nullptr;
    const TeacherDecision decision =
        tracker_->update(sig.verdict, reason, studyingPtr, usedApi);

    if (decision.state == TeacherState::Focusing) {
        ++stats_->focusTicks;
    } else if (decision.state == TeacherState::Distracted) {
        ++stats_->distractedTicks;
    } else {
        ++stats_->unsureTicks;
    }

    // ---- 告警（托盘通知 + 提示音由主线程发）
    if (decision.shouldAlert) {
        ++stats_->alerts;
        qInfo().noquote() << QStringLiteral("[teacher] 告警 | %1 | 目标=%2")
                                 .arg(decision.reason, currentGoal());
        emit alert(decision.reason, currentGoal());
    }

    // ---- 日志与广播（不记录图片内容与请求体，只记字节数）
    qInfo().noquote()
        << QStringLiteral("[teacher][%1] %2 %3 %4 间隔=%5s 人=%6 已用=%7s tok=%8+%9")
               .arg(stats_->screenshots)
               .arg(decision.state == TeacherState::Focusing
                        ? QStringLiteral("focusing")
                        : decision.state == TeacherState::Distracted ? QStringLiteral("distracted")
                                                                     : QStringLiteral("unsure"),
                    decision.usedApi ? QStringLiteral("模型") : QStringLiteral("本地"),
                    decision.reason)
               .arg(int(decision.nextIntervalS))
               .arg(sig.personPresent.has_value()
                        ? (*sig.personPresent ? QStringLiteral("在场") : QStringLiteral("离开"))
                        : QStringLiteral("n/a"))
               .arg(int(stats_->elapsedS()))
               .arg(judge_->lastPromptTokens())
               .arg(judge_->lastCompletionTokens());

    emitStatus(decision, sig);
    return decision;
}

void TeacherMonitor::emitStatus(const TeacherDecision& d, const TeacherSignals& sig) {
    TeacherStatus st;
    st.state = d.state;
    st.reason = d.reason;
    st.elapsedS = stats_->elapsedS();
    st.nextIntervalS = d.nextIntervalS;
    st.personPresent = sig.personPresent.has_value() && *sig.personPresent;
    st.hasPersonInfo = sig.personPresent.has_value();
    st.usedApi = d.usedApi;
    st.pomodoroEnabled = pomodoro_->enabled();
    if (st.pomodoroEnabled) {
        const TeacherPomodoroState ps = pomodoro_->state();
        st.phase = ps.phase;
        st.pomodoroRemainingS = ps.remainingS;
        st.pomodoroTotalS = ps.totalS;
        st.pomodoroRounds = ps.roundsDone;
        st.inBreak = pomodoro_->inBreak();
    }
    st.screenshots = stats_->screenshots;
    st.cameraShots = stats_->cameraShots;
    st.localHits = stats_->localHits;
    st.apiCalls = stats_->apiCalls;
    st.apiFailed = stats_->apiFailed;
    st.apiSkipped = stats_->apiSkipped;
    st.focusTicks = stats_->focusTicks;
    st.distractedTicks = stats_->distractedTicks;
    st.unsureTicks = stats_->unsureTicks;
    st.alerts = stats_->alerts;
    st.promptTokens = stats_->promptTokens;
    st.completionTokens = stats_->completionTokens;
    emit statusChanged(st);
}

void TeacherMonitor::enterBreakPhase() {
    // 进入休息阶段：弹右下角提示，然后开始倒计时。
    // break_minutes == 0 时直接回专注（advance 已处理），不提示。
    if (cfg_.pomodoro.breakMinutes <= 0) return;

    const double mins = cfg_.pomodoro.breakMinutes;
    const QString msg = QStringLiteral("已专注 %1 分钟，休息 %2 分钟吧。\n休息期间不会抓拍。")
                            .arg(int(cfg_.pomodoro.focusMinutes))
                            .arg(int(mins));
    qInfo().noquote() << QStringLiteral("[teacher] 进入休息 %1 分钟，第 %2 轮完成")
                             .arg(int(mins))
                             .arg(pomodoro_->roundsDone());
    emit pomodoroBreak(msg);
}

void TeacherMonitor::runBreak() {
    // 休息阶段：只倒计时，不截图不拍照。每秒检查一次停止标志，
    // 保证「点停止」能立刻退出，不会傻等 10 分钟。
    while (!stopRequested_.load(std::memory_order_acquire)) {
        pomodoro_->updateElapsed();
        if (pomodoro_->phaseExpired()) {
            // 累计休息时长要在 advance() **之前**取（advance 后 phase 变成专注，
            // state().totalS 就是专注时长了）。参考实现在这里加错过，不跟着错。
            const double breakTotal = pomodoro_->state().totalS;
            pomodoro_->advance();
            pomodoro_->addBreakTime(breakTotal);
            stats_->pauseSeconds += breakTotal;
            const TeacherPomodoroState st = pomodoro_->state();
            stats_->pomodoroRounds = pomodoro_->roundsDone();
            stats_->pomodoroUsed = true;
            const QString msg = QStringLiteral("休息结束，已完成 %1 轮专注。继续加油。")
                                    .arg(pomodoro_->roundsDone());
            qInfo().noquote() << QStringLiteral("[teacher] %1").arg(msg);
            emit pomodoroFocus(msg);
            emit pomodoroChanged(st);
            return;
        }
        waitSleep(1.0);
        if (pomodoro_->enabled()) emit pomodoroChanged(pomodoro_->state());
    }
}

void TeacherMonitor::shutdown() {
    // 幂等：正常退出路径只走一次
    if (stats_ && pomodoro_ && pomodoro_->enabled()) {
        stats_->pomodoroRounds = pomodoro_->roundsDone();
        stats_->pomodoroUsed = true;
    }

    TeacherReport report;
    report.goal = currentGoal();
    if (stats_) report.stats = *stats_;

    qInfo().noquote() << QStringLiteral("[teacher] 会话结束 | 目标=%1 | 截图 %2 | 告警 %3")
                             .arg(report.goal)
                             .arg(stats_ ? stats_->screenshots : 0)
                             .arg(stats_ ? stats_->alerts : 0);
    emit finished(report);
}

} // namespace pic2pet
