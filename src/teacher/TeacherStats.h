#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QVector>

namespace pic2pet {

/**
 * 会话统计：采集次数、本地/API 判定比例、token 与花费。
 *
 * 全部是进程内计数，不落数据库——结束时的报告照样准确。
 * 价格区分高峰/空闲时段（DeepSeek 高峰单价是空闲的 2 倍，算错会严重低估）。
 */
class TeacherStats {
public:
    TeacherStats();

    // 采集
    int screenshots = 0;    // 屏幕截图次数（每个 tick 必截一次）
    int cameraShots = 0;    // 摄像头拍照次数（关闭/不可用时为 0）
    int cameraMiss = 0;     // 摄像头读取失败次数

    // 判定来源
    int localHits = 0;      // L0 本地规则直接定案，未调 API
    int apiCalls = 0;       // 实际提交给模型的次数
    int apiFailed = 0;      // 调用失败（网络异常、解析失败）
    int apiSkipped = 0;     // 存疑但因缺 key 而跳过

    // 判定结果
    int focusTicks = 0;
    int distractedTicks = 0;
    int unsureTicks = 0;

    // 告警
    int alerts = 0;

    // token 与上传体积
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
    qint64 imageBytes = 0;

    // 课堂模式
    double pauseSeconds = 0.0;
    int pomodoroRounds = 0;
    bool pomodoroUsed = false;

    /** 总判定次数。**别漏掉 apiFailed**——失败也是一次真实发生的判定 */
    int totalTicks() const;
    double localRatio() const;
    double focusRatio() const;
    double avgPromptTokens() const;
    double imageMb() const;
    double elapsedS() const;
    QString elapsedFmt() const;   // "45:12" / "1:02:03"
    double avgIntervalS() const;

    /** 按当前时段价格估算花费（元） */
    double estCost() const;
    /** 同上，但可直接拿 token 数算（设置页状态卡用） */
    static double estCostFor(qint64 promptTokens, qint64 completionTokens);
    /** 当前是否高峰时段（北京时间：工作日 9:00-12:00 / 14:00-18:00） */
    static bool isPeakTime();

private:
    QElapsedTimer clock_;
};

/**
 * 单次会话报告：(标签, 值) 列表，分组顺序 采集 → 判定 → 结果 → 成本。
 * 结束时会话报告与设置页状态卡共用同一套格式化逻辑。
 */
QVector<QPair<QString, QString>> teacherFormatReport(const TeacherStats& st);

} // namespace pic2pet
