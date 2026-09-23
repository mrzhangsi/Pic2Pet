#pragma once

#include <QStringList>
#include <QWidget>

class QPropertyAnimation;

namespace pic2pet {

/**
 * iOS/macOS 风格分段控件（"关 弱 中 强"那种）。
 *
 * 自绘原因同 ToggleSwitch：Qt 没有这个控件，用一排 QRadioButton 怎么调都不像。
 * 选中块是一个会滑动的小白块（带动画），比"哪段变色"更像苹果。
 */
class SegmentedControl : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pill READ pill WRITE setPill)

public:
    explicit SegmentedControl(const QStringList& options, QWidget* parent = nullptr);

    int currentIndex() const { return current_; }
    /** 设置选中项；animated=false 时不做动画（初始化用，避免开窗时滑一下） */
    void setCurrentIndex(int index, bool animated = true);

    QSize sizeHint() const override;

    qreal pill() const { return pill_; }
    void setPill(qreal value);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void animateToTarget();
    int indexAt(const QPoint& pos) const;
    int segmentWidth() const;
    QRectF pillRect(qreal pill) const;

    QStringList options_;
    int current_ = 0;
    qreal pill_ = 0.0;       // 以"第几段"为单位，带小数 = 动画中间态
    QPropertyAnimation* anim_ = nullptr;
};

} // namespace pic2pet
