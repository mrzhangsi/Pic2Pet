#include "SidebarItem.h"

#include "Theme.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QPainter>

namespace pic2pet {

namespace {
constexpr int kHeight = 32;
constexpr int kRadius = 7;
constexpr int kIconSize = 16;
constexpr int kIconLeft = 10;
constexpr int kTextLeft = 34;
} // namespace

SidebarItem::SidebarItem(theme::Icon icon, const QColor& iconColor, const QString& text,
                         QWidget* parent)
    : QAbstractButton(parent), icon_(icon), iconColor_(iconColor) {
    setText(text);
    setCheckable(true);
    setAutoExclusive(true);          // 一列里同时只选中一个
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize SidebarItem::sizeHint() const {
    const QFontMetrics fm(font());
    return QSize(kTextLeft + fm.horizontalAdvance(text()) + 10, kHeight);
}

void SidebarItem::enterEvent(QEnterEvent* event) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void SidebarItem::leaveEvent(QEvent* event) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void SidebarItem::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool on = isChecked();

    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::Accent);
        p.drawRoundedRect(box, kRadius, kRadius);
    } else if (hover_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawRoundedRect(box, kRadius, kRadius);
    }

    // 图标
    const QRectF iconBox(kIconLeft, (height() - kIconSize) / 2.0, kIconSize, kIconSize);
    theme::paintIcon(p, icon_, iconBox, on ? QColor(0xFF, 0xFF, 0xFF) : iconColor_, 1.5);

    // 文字
    QFont f = font();
    f.setBold(on);
    p.setFont(f);
    p.setPen(on ? QColor(0xFF, 0xFF, 0xFF) : theme::MenuText);
    p.drawText(QRect(kTextLeft, 0, width() - kTextLeft - 8, height()),
               Qt::AlignVCenter | Qt::AlignLeft, text());
}

} // namespace pic2pet
