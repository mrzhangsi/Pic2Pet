#include "PetMotion.h"

#include <QtMath>

namespace pic2pet {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSettlePos = 1e-4;
constexpr double kSettleVel = 1e-3;
/** 打字时横向分量换算成倾斜：打左边的键身子往左歪多少度（峰值） */
constexpr double kTypingLeanDeg = 4.0;

} // namespace

void PetMotion::impulse(double amount, double lateral) {
    if (!params_.enabled) return;

    // 直接给**位移**、并把速度清零，让弹簧把它拉回去（快速按下 + 缓慢回弹，正是按键的手感）。
    //
    // 为什么不用"给初速度让弹簧积分上去"：那个峰值 ≈ 冲量/ω，对积分步长极其敏感——
    // 显式欧拉在 dt=33ms、ω≈15 时数值阻尼很重，实测峰值只有解析解的 1/3，
    // 而且系数改一次效果就变一次，很难预期。直接给位移则"设多少就是多少"，且连打能叠加。
    bounce_ = std::min(bounce_ + amount, 1.5);
    bounceVel_ = 0.0;
    lateral_ = std::clamp(lateral_ + lateral * 0.6, -1.0, 1.0);
    lateralVel_ = 0.0;
}

void PetMotion::setLeanTarget(double deg) {
    leanTarget_ = params_.enabled ? deg : 0.0;
}

bool PetMotion::step(double dt) {
    if (dt <= 0.0) return true;
    if (dt > 0.25) dt = 0.25;      // 卡顿后别一次跳太多

    breathT_ += dt;
    if (params_.breathPeriodSec > 0.0 && breathT_ > params_.breathPeriodSec) {
        breathT_ -= params_.breathPeriodSec;
    }

    // 弹簧：a = -k*x - c*v（显式欧拉，33ms 步长下 ω≈15 rad/s 是稳定的）
    const double k = params_.stiffness;
    const double c = params_.damping;

    bounceVel_ += (-k * bounce_ - c * bounceVel_) * dt;
    bounce_ += bounceVel_ * dt;

    lateralVel_ += (-k * lateral_ - c * lateralVel_) * dt;
    lateral_ += lateralVel_ * dt;

    // 倾斜：朝目标平滑逼近（不是弹簧，避免拖拽时来回晃）
    const double rate = 8.0;
    double t = rate * dt;
    if (t > 1.0) t = 1.0;
    lean_ += (leanTarget_ - lean_) * t;

    // 已经静止？（呼吸开着就不算静止——它是持续的）
    if (params_.breathAmp > 0.0) return true;
    if (qAbs(bounce_) < kSettlePos && qAbs(bounceVel_) < kSettleVel
        && qAbs(lateral_) < kSettlePos && qAbs(lateralVel_) < kSettleVel
        && qAbs(lean_ - leanTarget_) < 1e-3) {
        return false;
    }
    return true;
}

PetTransform PetMotion::transform() const {
    PetTransform t;
    if (!params_.enabled) return t;   // 关掉就是完全不变换

    // 呼吸：纵向起伏 + 轻微横缩（近似"体积守恒"，看着像在吸气）
    double breath = 0.0;
    if (params_.breathAmp > 0.0 && params_.breathPeriodSec > 0.0) {
        breath = params_.breathAmp * qSin(2.0 * kPi * breathT_ / params_.breathPeriodSec);
    }

    // 回弹：按键时纵向压扁（蹲一下）、横向撑开，并按"这个键在键盘上偏哪边"歪一点身子
    const double b = bounce_ * params_.bounceAmp;

    t.zoomX = 1.0 - 0.5 * breath + 0.6 * b;
    t.zoomY = 1.0 + breath - b;
    t.dx = lateral_ * params_.bounceAmp * 0.6;
    t.dy = 0.25 * breath - 0.35 * b;
    // 光平移太"滑"了，加个绕脚底的倾斜才像"人往那边歪"
    t.rotateDeg = lean_ + lateral_ * kTypingLeanDeg;
    return t;
}

} // namespace pic2pet
