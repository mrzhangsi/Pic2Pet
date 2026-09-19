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
    Typing,        // 输入模式：敲键盘时触发（播一次）
};

/** 一个动作 = 一段序列 + 播放方式 */
struct ActionClip {
    FrameSequence seq;
    bool loop = true;               // 只有待机/拖拽循环，其余播一次
    int holdMs = 600;               // 素材是**静态图**时的停留时长（毫秒），播完就回待机
    bool isValid() const { return seq.isValid(); }
};

/**
 * 动作集：一只宠物的全部动作。
 *
 * 只认**单个素材文件**，不依赖任何目录结构：
 *   - `load()` 给的那个文件 → 待机动作
 *   - 其余动作来自"动作设置"窗口的指定项（`setActionFromPath`）；没指定就没有该动作
 */
class ActionSet {
public:
    /** 把单个素材文件设为待机动作。失败时把原因写进 *error（error 可为 nullptr） */
    bool load(const QString& path, QString* error);

    bool has(PetAction a) const;
    /** 取动作；该动作缺失时自动回退到待机；待机也没有则返回无效 clip */
    const ActionClip& clip(PetAction a) const;
    /** 待机动作（窗口尺寸与兜底都按它来） */
    const ActionClip& idle() const { return idle_; }

    /** 单独指定某个动作的素材（"动作设置"窗口的指定项）。loop 按动作语义决定。
     *  holdMs 只在素材是静态图时生效。 */
    bool setActionFromPath(PetAction a, const QString& path, int holdMs, QString* error = nullptr);

    /** 只改某个动作的停留时长（素材不变） */
    void setHoldMs(PetAction a, int holdMs);

    /** 没有待机动作时，用第一个可用动作兜底。应用完指定项之后调用一次。 */
    void ensureIdle();

    bool isValid() const { return idle_.isValid(); }
    /** 所有动作的帧内存合计 */
    qint64 bytes() const;
    /** 日志用：已加载动作一览，例如 "idle(24f) click(12f)" */
    QString summary() const;

    static QString actionName(PetAction a);

private:
    ActionClip idle_;
    ActionClip click_;
    ActionClip doubleClick_;
    ActionClip drag_;
    ActionClip typing_;
};

} // namespace pic2pet
