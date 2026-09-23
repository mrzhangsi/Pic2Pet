#include "SegmentedControl.h"

#include "Theme.h"

#include <QEasingCurve>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include <algorithm>

namespace pic2pet {

namespace {
constexpr int kPad = 2;        // 内边距
constexpr int kSegPad = 14;    // 每段文字左右留白
constexpr int kHeight = 28;
constexpr int kDurationMs = 130;
} // namespace

SegmentedControl::SegmentedControl(const QStringList& options, QWidget* parent)
    : QWidget(parent), options_(options) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);

    // 各段等宽：取最宽的那段文字算，看着最整齐
    const QFontMetrics fm(font());
    int textWidth = 0;
    for (const QString& s : options_) textWidth = std::max(textWidth, fm.horizontalAdvance(s));
    const int segWidth = textWidth + kSegPad * 2;
    setFixedSize(segWidth * std::max(1, int(options_.size())) + kPad * 2, kHeight);

    pill_ = current_;   // 打开前先摆到位，别让用户看到它滑动

    anim_ = new QPropertyAnimation(this, "pill", this);
    anim_->setDuration(kDurationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
}

QSize SegmentedControl::sizeHint() const { return size(); }

int SegmentedControl::segmentWidth() const {
    if (options_.isEmpty()) return 0;
    return (width() - kPad * 2) / options_.size();
}

QRectF SegmentedControl::pillRect(qreal pill) const {
    const int sw = segmentWidth();
    return QRectF(kPad + pill * sw, kPad, sw, height() - kPad * 2);
}

void SegmentedControl::setCurrentIndex(int index, bool animated) {
    if (index < 0 || index >= options_.size() || index == current_) return;
    current_ = index;
    if (animated) {
        animateToTarget();
    } else {
        anim_->stop();
        setPill(current_);
    }
    emit currentChanged(current_);
}

void SegmentedControl::setPill(qreal value) {
    pill_ = value;
    update();
}

void SegmentedControl::animateToTarget() {
    anim_->stop();
    anim_->setStartValue(pill_);
    anim_->setEndValue(qreal(current_));
    anim_->start();
}

int SegmentedControl::indexAt(const QPoint& pos) const {
    const int sw = segmentWidth();
    if (sw <= 0) return -1;
    const int idx = (pos.x() - kPad) / sw;
    if (idx < 0 || idx >= options_.size()) return -1;
    return idx;
}

void SegmentedControl::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int idx = indexAt(event->pos());
    if (idx >= 0) setCurrentIndex(idx);
    event->accept();
}

void SegmentedControl::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal outerRadius = (height() - 0.0) / 2.0 - 0.5;

    p.setPen(Qt::NoPen);
    p.setBrush(theme::SegmentBg);
    p.drawRoundedRect(rect(), outerRadius, outerRadius);

    // 选中块（小白块，位置带动画）
    const QRectF pill = pillRect(pill_);
    p.setBrush(QColor(0xFF, 0xFF, 0xFF));
    p.setPen(QPen(theme::Border, 0.8));
    const qreal pillRadius = pill.height() / 2.0 - 1.0;
    p.drawRoundedRect(pill.adjusted(0.4, 0.4, -0.4, -0.4), pillRadius, pillRadius);

    // 文字
    const int sw = segmentWidth();
    for (int i = 0; i < options_.size(); ++i) {
        const QRectF seg(kPad + i * sw, kPad, sw, height() - kPad * 2);
        const bool selected = (i == current_);
        QFont f = font();
        f.setBold(selected);
        p.setFont(f);
        p.setPen(selected ? theme::TextPrimary : theme::MenuText);
        p.drawText(seg, Qt::AlignCenter, options_.at(i));
    }
}

} // namespace pic2pet
