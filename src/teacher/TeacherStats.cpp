#include "TeacherStats.h"

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

namespace pic2pet {

namespace {

// DeepSeek 官方定价（元 / 百万 tokens），deepseek-flash
constexpr double kPriceInPeak = 2.0;
constexpr double kPriceOutPeak = 8.0;
constexpr double kPriceInOffPeak = 1.0;
constexpr double kPriceOutOffPeak = 4.0;

// 中国全境单一时区 UTC+8 且无夏令时，用固定偏移即可。
constexpr int kCnOffsetSeconds = 8 * 3600;

QDateTime nowCn() {
    return QDateTime::currentDateTimeUtc().toOffsetFromUtc(kCnOffsetSeconds);
}

} // namespace

TeacherStats::TeacherStats() {
    clock_.start();
}

int TeacherStats::totalTicks() const {
    return localHits + apiCalls + apiFailed + apiSkipped;
}

double TeacherStats::localRatio() const {
    const int t = totalTicks();
    return t > 0 ? double(localHits) / double(t) : 0.0;
}

double TeacherStats::focusRatio() const {
    const int t = totalTicks();
    return t > 0 ? double(focusTicks) / double(t) : 0.0;
}

double TeacherStats::avgPromptTokens() const {
    return apiCalls > 0 ? double(promptTokens) / double(apiCalls) : 0.0;
}

double TeacherStats::imageMb() const {
    return double(imageBytes) / 1024.0 / 1024.0;
}

double TeacherStats::elapsedS() const {
    return clock_.elapsed() / 1000.0;
}

QString TeacherStats::elapsedFmt() const {
    const int s = int(elapsedS());
    const int h = s / 3600;
    const int m = (s % 3600) / 60;
    const int sec = s % 60;
    if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                                        .arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
                                   .arg(sec, 2, 10, QLatin1Char('0'));
}

double TeacherStats::avgIntervalS() const {
    const int t = totalTicks();
    return t > 0 ? elapsedS() / double(t) : 0.0;
}

double TeacherStats::estCostFor(qint64 promptTokens, qint64 completionTokens) {
    const bool peak = isPeakTime();
    const double inPrice = peak ? kPriceInPeak : kPriceInOffPeak;
    const double outPrice = peak ? kPriceOutPeak : kPriceOutOffPeak;
    return (double(promptTokens) * inPrice + double(completionTokens) * outPrice) / 1000000.0;
}

double TeacherStats::estCost() const {
    return estCostFor(promptTokens, completionTokens);
}

bool TeacherStats::isPeakTime() {
    const QDateTime now = nowCn();
    if (now.date().dayOfWeek() > 5) return false;   // 周末全天空闲
    const QTime t = now.time();
    const QTime a0(9, 0), a1(12, 0), b0(14, 0), b1(18, 0);
    return (a0 <= t && t < a1) || (b0 <= t && t < b1);
}

QVector<QPair<QString, QString>> teacherFormatReport(const TeacherStats& st) {
    QVector<QPair<QString, QString>> rows;
    const auto num = [](qint64 v) { return QLocale::c().toString(v); };

    rows.append({QStringLiteral("── 采集 ──"), QString()});
    rows.append({QStringLiteral("截图次数"), QStringLiteral("%1 次").arg(num(st.screenshots))});
    if (st.cameraShots || st.cameraMiss) {
        QString cam = QStringLiteral("%1 次").arg(num(st.cameraShots));
        if (st.cameraMiss) cam += QStringLiteral("（失败 %1 次）").arg(num(st.cameraMiss));
        rows.append({QStringLiteral("拍照次数"), cam});
    } else {
        rows.append({QStringLiteral("拍照次数"), QStringLiteral("0 次（摄像头未启用）")});
    }
    // 样本太少时平均间隔没意义（首 tick 含启动开销，会严重拉偏）
    if (st.totalTicks() >= 2) {
        rows.append({QStringLiteral("平均间隔"),
                     QStringLiteral("%1 秒").arg(num(qint64(st.avgIntervalS())))});
    }

    rows.append({QStringLiteral("── 判定来源 ──"), QString()});
    if (st.apiCalls || st.apiFailed || st.apiSkipped) {
        rows.append({QStringLiteral("本地规则定案"),
                     QStringLiteral("%1 次（占 %2%，零成本）")
                         .arg(num(st.localHits))
                         .arg(num(qint64(st.localRatio() * 100.0)))});
        rows.append({QStringLiteral("模型判定"),
                     QStringLiteral("%1 次（成功取到结果）").arg(num(st.apiCalls))});
        if (st.apiFailed) {
            rows.append({QStringLiteral("  模型失败"),
                         QStringLiteral("%1 次（未取到结果）").arg(num(st.apiFailed))});
        }
        if (st.apiSkipped) {
            rows.append({QStringLiteral("  缺 key 跳过"),
                         QStringLiteral("%1 次").arg(num(st.apiSkipped))});
        }
        rows.append({QStringLiteral("判定总次数"),
                     QStringLiteral("%1 次").arg(num(st.totalTicks()))});
        if (st.apiFailed || st.apiSkipped) {
            rows.append({QStringLiteral("  其中未出结果"),
                         QStringLiteral("%1 次（计入总数，但计入下方「不确定」）")
                             .arg(num(st.apiFailed + st.apiSkipped))});
        }
    } else {
        rows.append({QStringLiteral("本地规则定案"),
                     QStringLiteral("%1 次（100%，未调用模型）").arg(num(st.localHits))});
        rows.append({QStringLiteral("模型判定"), QStringLiteral("0 次（未触发模型调用）")});
        rows.append({QStringLiteral("判定总次数"),
                     QStringLiteral("%1 次").arg(num(st.totalTicks()))});
    }

    rows.append({QStringLiteral("── 判定结果 ──"), QString()});
    rows.append({QStringLiteral("学习中"), QStringLiteral("%1 次").arg(num(st.focusTicks))});
    rows.append({QStringLiteral("分心"), QStringLiteral("%1 次").arg(num(st.distractedTicks))});
    rows.append({QStringLiteral("不确定"), QStringLiteral("%1 次").arg(num(st.unsureTicks))});
    rows.append({QStringLiteral("专注比例"),
                 QStringLiteral("%1%").arg(num(qint64(st.focusRatio() * 100.0)))});
    rows.append({QStringLiteral("告警次数"), QStringLiteral("%1 次").arg(num(st.alerts))});

    if (st.pomodoroUsed || st.pomodoroRounds || st.pauseSeconds > 0.0) {
        rows.append({QStringLiteral("── 课堂模式 ──"), QString()});
        rows.append({QStringLiteral("完成轮次"),
                     QStringLiteral("%1 轮").arg(num(st.pomodoroRounds))});
        if (st.pauseSeconds > 0.0) {
            const int mins = int(st.pauseSeconds) / 60;
            const int secs = int(st.pauseSeconds) % 60;
            const QString txt = mins > 0 ? QStringLiteral("%1 分钟").arg(mins)
                                         : QStringLiteral("%1 秒").arg(secs);
            rows.append({QStringLiteral("累计休息"),
                         QStringLiteral("%1（期间未抓拍）").arg(txt)});
        }
    }

    rows.append({QStringLiteral("── 成本 ──"), QString()});
    if (st.apiCalls) {
        rows.append({QStringLiteral("token 消耗"),
                     QStringLiteral("输入 %1 / 输出 %2")
                         .arg(QLocale::c().toString(st.promptTokens))
                         .arg(QLocale::c().toString(st.completionTokens))});
        rows.append({QStringLiteral("单次平均输入"),
                     QStringLiteral("%1 tokens").arg(num(qint64(st.avgPromptTokens())))});
        rows.append({QStringLiteral("预估花费"),
                     QStringLiteral("¥%1").arg(st.estCost(), 0, 'f', 5)});
        if (st.imageBytes > 0) {
            rows.append({QStringLiteral("图片上传量"),
                         QStringLiteral("%1 MB").arg(st.imageMb(), 0, 'f', 2)});
        }
    } else {
        rows.append({QStringLiteral("token 消耗"), QStringLiteral("0（未调用模型）")});
        rows.append({QStringLiteral("预估花费"), QStringLiteral("¥0.00000")});
    }
    return rows;
}

} // namespace pic2pet
