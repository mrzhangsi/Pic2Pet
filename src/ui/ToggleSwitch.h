#pragma once

#include <QAbstractButton>

class QPropertyAnimation;

namespace pic2pet {

/**
 * iOS 风格开关。
 *
 * 为什么自绘：QCheckBox 那套方块勾选在苹果风里完全不成立，而 QSS 改不出圆角轨道 +
 * 滑动圆钮（QSS 没有位置动画）。所以这里直接画：44x26 圆角轨道 + 白色圆钮，状态切换带动画。
 */
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    qreal knob() const { return knob_; }
    void setKnob(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void checkStateSet() override { animate(); }
    void nextCheckState() override;

private:
    void animate();

    qreal knob_ = 0.0;                      // 0 = 关，1 = 开
    QPropertyAnimation* anim_ = nullptr;
};

} // namespace pic2pet
