#include "TeacherPomodoro.h"

#include <QStringLiteral>

namespace pic2pet {

TeacherPomodoro::TeacherPomodoro(double focusMinutes, double breakMinutes, bool enabled)
    : enabled_(enabled),
      focusS_(qMax(1.0, focusMinutes * 60.0)),
      breakS_(qMax(0.0, breakMinutes * 60.0)) {
    clock_.start();
    phaseStartedMs_ = 0.0;
}

void TeacherPomodoro::updateElapsed() {
    // 即使中间停了 30 分钟也能正确累计，不会出现「重启就重置」的问题。
    elapsedInPhase_ += clock_.elapsed() / 1000.0 - phaseStartedMs_ / 1000.0;
    phaseStartedMs_ = clock_.elapsed();
}

double TeacherPomodoro::remaining(double total) const {
    return qMax(0.0, total - elapsedInPhase_);
}

TeacherPomodoroState TeacherPomodoro::state() const {
    TeacherPomodoroState st;
    st.phase = phase_;
    st.totalS = phase_ == TeacherPhase::Focus ? focusS_ : breakS_;
    st.remainingS = remaining(st.totalS);
    st.roundsDone = roundsDone_;
    st.breakTotalS = breakTotalS_;
    return st;
}

bool TeacherPomodoro::phaseExpired() const {
    if (phase_ == TeacherPhase::Focus) return elapsedInPhase_ >= focusS_;
    return breakS_ > 0 && elapsedInPhase_ >= breakS_;
}

TeacherPhase TeacherPomodoro::advance() {
    if (phase_ == TeacherPhase::Focus) {
        ++roundsDone_;
        phase_ = breakS_ <= 0 ? TeacherPhase::Focus : TeacherPhase::Break;
    } else {
        phase_ = TeacherPhase::Focus;
    }
    elapsedInPhase_ = 0.0;
    phaseStartedMs_ = clock_.elapsed();
    return phase_;
}

void TeacherPomodoro::addBreakTime(double seconds) {
    breakTotalS_ += qMax(0.0, seconds);
}

QString TeacherPomodoro::describe() const {
    if (!enabled_) return QStringLiteral("课堂模式未启用");
    const TeacherPomodoroState st = state();
    if (st.phase == TeacherPhase::Focus) {
        return QStringLiteral("专注中，剩余 %1").arg(st.remainingFmt());
    }
    return QStringLiteral("休息中，剩余 %1").arg(st.remainingFmt());
}

double TeacherPomodoroState::progress() const {
    if (totalS <= 0) return 1.0;
    const double done = totalS - qMax(0.0, remainingS);
    return qMin(1.0, qMax(0.0, done / totalS));
}

QString TeacherPomodoroState::remainingFmt() const {
    const int s = int(qMax(0.0, remainingS));
    const int h = s / 3600;
    const int m = (s % 3600) / 60;
    const int sec = s % 60;
    if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                                        .arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
                                   .arg(sec, 2, 10, QLatin1Char('0'));
}

QString TeacherPomodoroState::phaseLabel() const {
    return phase == TeacherPhase::Focus ? QStringLiteral("专注中") : QStringLiteral("休息中");
}

} // namespace pic2pet
