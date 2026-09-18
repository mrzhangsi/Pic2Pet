#pragma once

#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/** 宠物动作。待机是默认动作，其余都是"被触发一次、播完回待机"。 */
enum class PetAction {
    Idle,          // 待机（循环，默认）
    Click,         // 单击
    DoubleClick,   // 双击
    Drag,          // 拖拽中（循环，松手回待机）
};

/** 一个动作 = 一段序列 + 播放方式 */
struct ActionClip {
    FrameSequence seq;
    bool loop = true;               // 只有待机/拖拽循环，其余播一次
    bool isValid() const { return seq.isValid(); }
};

/**
 * 动作集：把"一只宠物的全部动作"打包起来。
 *
 * 目录约定（放在同一个文件夹里，**名字**决定它是哪个动作，扩展名随意）：
 *
 *     <宠物目录>/
 *         idle.gif | idle/ | 待机.gif      → 待机（默认动作）
 *         click.gif | click/ | 点击.gif    → 单击
 *         doubleclick.gif | 双击.gif      → 双击
 *         drag.gif | 拖拽.gif             → 拖拽中
 *
 * 容错：
 *   - 只给**单个文件**，或目录里一个约定名都没认出来 → 整份当作待机动作，
 *     行为与 M1 完全一致（向后兼容）。
 *   - 没有待机动作时，用第一个可用的动作兜底，避免"加载成功却什么都不显示"。
 */
class ActionSet {
public:
    /** 从文件或目录加载。失败时把原因写进 *error（error 可为 nullptr） */
    bool load(const QString& path, QString* error);

    bool has(PetAction a) const;
    /** 取动作；该动作缺失时自动回退到待机；待机也没有则返回无效 clip */
    const ActionClip& clip(PetAction a) const;
    /** 待机动作（窗口尺寸与兜底都按它来） */
    const ActionClip& idle() const { return idle_; }

    bool isValid() const { return idle_.isValid(); }
    /** 所有动作的帧内存合计 */
    qint64 bytes() const;

    /** 日志用：已加载动作一览，例如 "idle(24f) click(12f)" */
    QString summary() const;

    static QString actionName(PetAction a);
    /** 从文件/目录名解析动作类型；不认识则返回 false */
    static bool actionFromName(const QString& baseName, PetAction* out);

private:
    ActionClip idle_;
    ActionClip click_;
    ActionClip doubleClick_;
    ActionClip drag_;
};

} // namespace pic2pet
