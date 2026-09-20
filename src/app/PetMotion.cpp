#include "PetMotion.h"

#include <QtMath>

namespace pic2pet {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSettlePos = 1e-4;
constexpr double kSettleVel = 1e-3;

} // namespace

void PetMotion::impulse(double amount, double lateral) {
    if (!params_.enabled) return;

    // 单位是"幅度倍数"：spring 会把它们变成带过冲的收敛过程
    bounceVel_ += amount * 3.0;
    lateralVel_ += lateral * 2.0;
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

    // 回弹：按键时纵向压扁一点（蹲一下），横向撑开一点
    const double b = bounce_ * params_.bounceAmp;

    t.zoomX = 1.0 - 0.5 * breath + 0.6 * b;
    t.zoomY = 1.0 + breath - b;
    t.dx = lateral_ * params_.bounceAmp * 1.5;
    t.dy = 0.25 * breath - 0.35 * b;
    t.rotateDeg = lean_;
    return t;
}

} // namespace pic2pet
