#include "ToggleSwitch.h"

#include "Theme.h"

#include <QEasingCurve>
#include <QPainter>
#include <QPropertyAnimation>

namespace pic2pet {

namespace {
constexpr int kWidth = 44;
constexpr int kHeight = 26;
constexpr int kMargin = 2;                 // 圆钮到轨道边的间距
constexpr int kDurationMs = 140;
} // namespace

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kWidth, kHeight);

    anim_ = new QPropertyAnimation(this, "knob", this);
    anim_->setDuration(kDurationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, &QAbstractButton::toggled, this, [this] { animate(); });
    setKnob(isChecked() ? 1.0 : 0.0);
}

QSize ToggleSwitch::sizeHint() const { return QSize(kWidth, kHeight); }

void ToggleSwitch::setKnob(qreal value) {
    knob_ = value;
    update();
}

void ToggleSwitch::nextCheckState() {
    QAbstractButton::nextCheckState();   // 触发 toggled → animate()
}

void ToggleSwitch::animate() {
    anim_->stop();
    anim_->setStartValue(knob_);
    anim_->setEndValue(isChecked() ? 1.0 : 0.0);
    anim_->start();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = height() / 2.0;

    // 轨道：关是灰、开是绿，中间过渡直接按 knob_ 线性插值
    QColor track = theme::TrackBg;
    if (knob_ > 0.0) {
        const QColor off(0xE9, 0xE9, 0xEB);
        track = QColor(int(off.red() + (theme::SwitchOn.red() - off.red()) * knob_),
                       int(off.green() + (theme::SwitchOn.green() - off.green()) * knob_),
                       int(off.blue() + (theme::SwitchOn.blue() - off.blue()) * knob_));
        if (knob_ >= 0.999) track = theme::SwitchOn;
    } else {
        track = QColor(0xE9, 0xE9, 0xEB);
    }
    if (!isEnabled()) track = QColor(0xEE, 0xEE, 0xEF);

    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(rect(), radius, radius);

    // 圆钮
    const qreal knobSize = height() - kMargin * 2.0;
    const qreal travel = width() - knobSize - kMargin * 2.0;
    const QRectF knobRect(kMargin + travel * knob_, kMargin, knobSize, knobSize);

    p.setBrush(QColor(0xFF, 0xFF, 0xFF));
    p.setPen(QPen(QColor(0, 0, 0, 24), 1.0));
    p.drawEllipse(knobRect);
}

} // namespace pic2pet
