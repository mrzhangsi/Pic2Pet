#pragma once

#include "PetRenderer.h"   // PetTransform

namespace pic2pet {

/**
 * 动态通道：呼吸 + 打字回弹。
 *
 * 这是"**不用 Live2D 也能有跟手感**"的核心。Live2D 的本质不是模型格式，
 * 而是"可叠加的参数通道"，所以这里照搬它的思路、但不做网格变形：
 *   - 两个通道**各算各的值**，最后叠加 —— 呼吸不会因为打字而中断，打字也不会重置呼吸；
 *   - 回弹用**弹簧**而不是"播一段动画"，所以连打时会自然叠加、停手时自然收敛；
 *   - `step()` 返回 false 表示已经静止，调用方据此停掉定时器，
 *     静态素材"空转 0% CPU"就靠这个（呼吸开着时就一直有得动，不会停）。
 *
 * 曾经还有"按键位左右歪"和"拖拽倾斜"两个倾斜通道，实测调大也不明显，已去掉 ——
 * 现在只保留"一弹一弹"。倾斜能力在 `PetTransform.rotateDeg` 里仍然保留，留给以后分层精灵用。
 */
class PetMotion {
public:
    struct Params {
        bool enabled = true;
        double breathAmp = 0.015;       // 呼吸幅度（缩放比例）；0 = 不呼吸
        double breathPeriodSec = 3.2;   // 呼吸周期（秒）
        double bounceAmp = 0.08;        // 单次按键的回弹幅度（压扁比例）
        double stiffness = 220.0;       // 弹簧刚度
        double damping = 18.0;          // 弹簧阻尼（欠阻尼，约 0.7s 回稳）
    };

    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    /** 敲键：让弹簧"+1 档位移"，随后自然回落。连打会叠加。 */
    void impulse(double amount = 1.0);

    /** 推进 dt 秒。返回 false 表示已经静止（调用方可以停定时器） */
    bool step(double dt);

    /** 各通道叠加后的变换 */
    PetTransform transform() const;

private:
    Params params_;

    double breathT_ = 0.0;
    double bounce_ = 0.0;        // 回弹位移（正 = 被压扁）
    double bounceVel_ = 0.0;
};

} // namespace pic2pet
