#pragma once

#include <optional>

#include <QImage>
#include <QMetaType>
#include <QString>

#include "TeacherConfig.h"

namespace pic2pet {

/** L0 本地规则结论（对齐参考实现的四级） */
enum class TeacherVerdict {
    Focus,     // 明确在学习（本实现未使用：本地不判"在学"，只判"不在"）
    Suspect,   // 存疑，交给模型
    Distract,  // 明确不在学习（确定性高，立即告警）
    Unknown,   // 拿不到本地信号
};

/** 一次 evaluate 采集到的本地信号 */
struct TeacherSignals {
    TeacherVerdict verdict = TeacherVerdict::Unknown;
    QString reason;
    std::optional<bool> personPresent;    // 摄像头关闭 / 读不到画面时 nullopt
    QString personReason;
    int absentStreak = 0;
    std::optional<bool> screenChanged;    // 首帧无基准时 nullopt
    std::optional<double> screenDiff;
    bool screenLocked = false;
    std::optional<double> idleS;
    double skinRatio = 0.0;
};

/**
 * L0 本地规则层：零 API 成本的确定性判定。
 *
 * 这一层的存在意义：锁屏这类事件是确定性的，用规则判比丢一张黑屏给视觉模型
 * 又快又准、还省一次调用。
 *
 * **只有确认「人不在」才能本地定案，其余一律交给模型看屏幕** ——
 * 「画面活跃」不等于「在学习」（打游戏时画面同样剧烈变化），本地规则区分不了。
 *
 * 在场检测：**肤色占比兜底**（不引 OpenCV）。实测人在 ~40%、空背景 ~5%，
 * 区分度约 8 倍且帧间稳定。Haar 级联暂不移植（俯拍低头场景本就漏检，
 * 肤色通道正是为兜这个场景而存在）。
 */
class TeacherRules {
public:
    TeacherRules(const TeacherRuleCfg& rulesCfg, const TeacherCaptureCfg& captureCfg);

    TeacherSignals evaluate(const QImage& screen, const QImage* camera);

    /** 重置帧差基准（启动 / 恢复监控时调用） */
    void reset();

    /** 肤色像素占比（下半画面）：人在 ~40%，空背景 ~5% */
    static double skinRatio(const QImage& frame);

private:
    std::pair<TeacherVerdict, QString> decide(const TeacherSignals& sig) const;

    TeacherRuleCfg cfg_;
    int diffWidth_ = 160;
    double diffThreshold_ = 6.0;
    QImage prevThumb_;
    int absentHits_ = 0;
};

#ifdef Q_OS_WIN
/** 键鼠空闲秒数（GetLastInputInfo）。拿不到返回 nullopt */
std::optional<double> teacherIdleSeconds();
/** 是否处于锁屏 / UAC 安全桌面（OpenInputDesktop 返回 NULL） */
bool teacherScreenLocked();
#else
inline std::optional<double> teacherIdleSeconds() { return std::nullopt; }
inline bool teacherScreenLocked() { return false; }
#endif

} // namespace pic2pet
