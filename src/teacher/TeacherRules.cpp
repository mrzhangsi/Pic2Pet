#include "TeacherRules.h"

#include "TeacherCapture.h"

#ifdef Q_OS_WIN
// windows.h 里的 min/max 宏会破坏 std::min / std::max，必须先禁掉
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <utility>

namespace pic2pet {

#ifdef Q_OS_WIN

std::optional<double> teacherIdleSeconds() {
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return std::nullopt;
    // GetTickCount 与 dwTime 同基准，都是开机毫秒数
    const DWORD now = GetTickCount();
    return qMax(0.0, double(now - info.dwTime) / 1000.0);
}

bool teacherScreenLocked() {
    // 锁屏时输入桌面被换成 secure desktop，OpenInputDesktop 返回 NULL。
    // 普通前台窗口下它总能打开。DESKTOP_SWITCHDESKTOP = 0x0100，只读打开即可。
    const HDESK desk = OpenInputDesktop(0, FALSE, 0x0100);
    if (!desk) return true;
    CloseDesktop(desk);
    return false;
}

#endif

TeacherRules::TeacherRules(const TeacherRuleCfg& rulesCfg, const TeacherCaptureCfg& captureCfg)
    : cfg_(rulesCfg), diffWidth_(captureCfg.diffWidth), diffThreshold_(captureCfg.diffThreshold) {}

void TeacherRules::reset() {
    prevThumb_ = QImage();
    absentHits_ = 0;
}

double TeacherRules::skinRatio(const QImage& frame) {
    if (frame.isNull()) return 0.0;
    // 只看下 2/3 区域：俯拍时人脸和上半身总是落在画面下部，上部多为背景。
    const int h = frame.height();
    const int y0 = int(h * 0.38);
    if (y0 >= h) return 0.0;

    const QImage img = frame.format() == QImage::Format_RGB32 || frame.format() == QImage::Format_ARGB32
                           ? frame
                           : frame.convertToFormat(QImage::Format_RGB32);

    // 肤色阈值（YCrCb 空间经典取值，对亚洲肤色友好）
    constexpr double kCrLo = 133.0, kCrHi = 173.0;
    constexpr double kCbLo = 77.0, kCbHi = 127.0;

    qint64 hit = 0;
    qint64 total = 0;
    for (int y = y0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double r = qRed(line[x]);
            const double g = qGreen(line[x]);
            const double b = qBlue(line[x]);
            const double ycc = 0.299 * r + 0.587 * g + 0.114 * b;
            const double cr = 128.0 + (r - ycc) * 0.713;
            const double cb = 128.0 + (b - ycc) * 0.564;
            if (cr >= kCrLo && cr <= kCrHi && cb >= kCbLo && cb <= kCbHi) ++hit;
            ++total;
        }
    }
    return total > 0 ? double(hit) / double(total) : 0.0;
}

TeacherSignals TeacherRules::evaluate(const QImage& screen, const QImage* camera) {
    TeacherSignals sig;

    sig.idleS = teacherIdleSeconds();
    sig.screenLocked = teacherScreenLocked();

    // 帧差：判断画面是否有变化（第一帧没有基准）
    if (!screen.isNull()) {
        const QImage thumb = teacherToThumb(screen, diffWidth_);
        if (!prevThumb_.isNull()) {
            const double diff = teacherGrayDiff(prevThumb_, thumb);
            if (diff >= 0.0) {
                sig.screenDiff = diff;
                sig.screenChanged = diff > diffThreshold_;
            }
        }
        prevThumb_ = thumb;
    }

    // 在场检测：肤色占比 + 连续帧确认防抖
    if (camera && !camera->isNull()) {
        const double ratio = skinRatio(*camera);
        const bool here = ratio >= cfg_.skinRatioMin;
        sig.skinRatio = ratio;
        sig.personPresent = here;
        sig.personReason = here ? QStringLiteral("肤色 %1%").arg(int(ratio * 100))
                                : QStringLiteral("未检测到人");
        // 连续 absenceConfirm 帧「不在场」才判离开。
        // 单帧可能因为光线 / 姿势临界而抖动，直接判离开会误报。
        absentHits_ = here ? 0 : absentHits_ + 1;
        sig.absentStreak = absentHits_;
    }

    const auto [verdict, reason] = decide(sig);
    sig.verdict = verdict;
    sig.reason = reason;
    return sig;
}

std::pair<TeacherVerdict, QString> TeacherRules::decide(const TeacherSignals& sig) const {
    // 硬规则 1：锁屏 → 立即判分心，不花钱
    if (sig.screenLocked) {
        return {TeacherVerdict::Distract, QStringLiteral("屏幕已锁定")};
    }

    // 硬规则 2：确认不在场 → 离开。这是唯一能在本地定案的场景
    if (sig.personPresent.has_value() && !*sig.personPresent) {
        if (sig.absentStreak >= cfg_.absenceConfirm) {
            return {TeacherVerdict::Distract,
                    QStringLiteral("连续 %1 帧%2").arg(sig.absentStreak).arg(sig.personReason)};
        }
    }

    // 其余一律交给模型看屏幕。
    if (sig.personPresent.has_value() && *sig.personPresent) {
        return {TeacherVerdict::Suspect,
                QStringLiteral("在场（%1），请模型看屏幕").arg(sig.personReason)};
    }
    return {TeacherVerdict::Suspect, QStringLiteral("需模型判定")};
}

} // namespace pic2pet
