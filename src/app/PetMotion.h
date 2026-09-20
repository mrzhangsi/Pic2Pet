#pragma once

#include "PetRenderer.h"   // PetTransform

namespace pic2pet {

/**
 * 动态通道：呼吸 / 打字回弹 / 拖拽倾斜。
 *
 * 这是"**不用 Live2D 也能做出跟手效果**"的核心。Live2D 的本质不是模型格式，
 * 而是"可叠加的参数通道"，所以这里照搬它的思路、但不做网格变形：
 *   - 每个通道**各算各的值**，最后叠加 —— 呼吸不会因为打字而中断，打字也不会重置呼吸；
 *   - 回弹用**弹簧**而不是"播一段动画"，所以连打时会自然叠加、停手时自然收敛；
 *   - `step()` 返回 false 表示已经静止，调用方据此停掉定时器，
 *     静态素材"空转 0% CPU"就靠这个（呼吸开着时就一直有得动，不会停）。
 */
class PetMotion {
public:
    struct Params {
        bool enabled = true;
        double breathAmp = 0.012;       // 呼吸幅度（缩放比例）；0 = 不呼吸
        double breathPeriodSec = 3.2;   // 呼吸周期（秒）
        double bounceAmp = 0.05;        // 单次按键的回弹幅度
        double leanDeg = 6.0;           // 拖拽时最大倾斜（度）
        double stiffness = 220.0;       // 弹簧刚度
        double damping = 18.0;          // 弹簧阻尼（欠阻尼，约 0.5s 收敛）
    };

    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    /** 敲键：给弹簧一个冲量。lateral ∈ [-1,1] 表示按的是键盘偏左还是偏右 */
    void impulse(double amount = 1.0, double lateral = 0.0);

    /** 拖拽倾斜目标（度，正负号表示方向）；0 = 回正 */
    void setLeanTarget(double deg);

    /** 推进 dt 秒。返回 false 表示已经静止（调用方可以停定时器） */
    bool step(double dt);

    /** 各通道叠加后的变换 */
    PetTransform transform() const;

private:
    Params params_;

    double breathT_ = 0.0;
    double bounce_ = 0.0;        // 竖直回弹（正 = 向上弹）
    double bounceVel_ = 0.0;
    double lateral_ = 0.0;       // 横向偏移
    double lateralVel_ = 0.0;
    double lean_ = 0.0;          // 当前倾斜（度）
    double leanTarget_ = 0.0;
};

} // namespace pic2pet
