#include "KeyBubble.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>

#include <algorithm>

namespace pic2pet {

namespace {

constexpr int kPadX = 20;        // 文字左右留白
constexpr int kPadY = 11;        // 文字上下留白
constexpr int kTailH = 8;        // 小尾巴高度
constexpr qreal kRadius = 14.0;  // 圆角半径
constexpr int kHoldMs = 950;     // 停留多久开始淡出
constexpr int kFadeMs = 260;     // 淡出时长
constexpr int kMinWidth = 58;

// 只用到的那几个虚拟键码。这里自己定义而不是 include <windows.h>，
// 好让 keyLabel() 在没有 Windows SDK 的平台上也能编译（数值是 Windows 规定的固定值）。
constexpr quint32 kVkBack = 0x08;
constexpr quint32 kVkTab = 0x09;
constexpr quint32 kVkReturn = 0x0D;
constexpr quint32 kVkShift = 0x10;
constexpr quint32 kVkControl = 0x11;
constexpr quint32 kVkMenu = 0x12;      // Alt
constexpr quint32 kVkEscape = 0x1B;
constexpr quint32 kVkSpace = 0x20;
constexpr quint32 kVkLeft = 0x25;
constexpr quint32 kVkUp = 0x26;
constexpr quint32 kVkRight = 0x27;
constexpr quint32 kVkDown = 0x28;
constexpr quint32 kVkF1 = 0x70;
constexpr quint32 kVkF12 = 0x7B;
constexpr quint32 kVkOem1 = 0xBA;      // ;
constexpr quint32 kVkOemPlus = 0xBB;   // =
constexpr quint32 kVkOemComma = 0xBC;  // ,
constexpr quint32 kVkOemMinus = 0xBD;  // -
constexpr quint32 kVkOemPeriod = 0xBE; // .
constexpr quint32 kVkOem2 = 0xBF;      // /
constexpr quint32 kVkOem3 = 0xC0;      // `
constexpr quint32 kVkOem4 = 0xDB;      // [
constexpr quint32 kVkOem5 = 0xDC;      // backslash
constexpr quint32 kVkOem6 = 0xDD;      // ]
constexpr quint32 kVkOem7 = 0xDE;      // '

} // namespace

QString KeyBubble::keyLabel(quint32 vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return QChar(char(vk));

    switch (vk) {
    case kVkSpace:   return QStringLiteral("空格");
    case kVkReturn:  return QStringLiteral("回车");
    case kVkBack:    return QStringLiteral("退格");
    case kVkTab:     return QStringLiteral("Tab");
    case kVkEscape:  return QStringLiteral("Esc");
    case kVkShift:   return QStringLiteral("Shift");
    case kVkControl: return QStringLiteral("Ctrl");
    case kVkMenu:    return QStringLiteral("Alt");
    case kVkLeft:    return QStringLiteral("←");
    case kVkRight:   return QStringLiteral("→");
    case kVkUp:      return QStringLiteral("↑");
    case kVkDown:    return QStringLiteral("↓");
    case kVkOem1:    return QStringLiteral(";");
    case kVkOem2:    return QStringLiteral("/");
    case kVkOem3:    return QStringLiteral("`");
    case kVkOem4:    return QStringLiteral("[");
    case kVkOem5:    return QStringLiteral("\\");
    case kVkOem6:    return QStringLiteral("]");
    case kVkOem7:    return QStringLiteral("'");
    case kVkOemPlus: return QStringLiteral("=");
    case kVkOemMinus:return QStringLiteral("-");
    case kVkOemComma:return QStringLiteral(",");
    case kVkOemPeriod: return QStringLiteral(".");
    default:
        break;
    }
    if (vk >= kVkF1 && vk <= kVkF12) return QStringLiteral("F%1").arg(vk - kVkF1 + 1);
    return QString();
}

KeyBubble::KeyBubble(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() + 4.0);
    f.setBold(true);
    setFont(f);

    holdTimer_ = new QTimer(this);
    holdTimer_->setSingleShot(true);
    connect(holdTimer_, &QTimer::timeout, this, [this] {
        fade_->stop();
        fade_->setStartValue(windowOpacity());
        fade_->setEndValue(0.0);
        fade_->start();
    });

    fade_ = new QPropertyAnimation(this, "windowOpacity", this);
    fade_->setDuration(kFadeMs);
    connect(fade_, &QPropertyAnimation::finished, this, [this] {
        if (windowOpacity() <= 0.01) hide();
    });
}

void KeyBubble::showKey(quint32 vk) {
    const QString label = keyLabel(vk);
    if (label.isEmpty()) return;      // 认不出来的键（多媒体键等）就不弹

    if (label != text_) {
        text_ = label;
        const QFontMetrics fm(font());
        const int w = fm.horizontalAdvance(text_) + kPadX * 2;
        const int h = fm.height() + kPadY * 2 + kTailH;
        setFixedSize(std::max(w, kMinWidth), h);
    }

    fade_->stop();
    setWindowOpacity(1.0);
    if (!isVisible()) show();
    raise();
    holdTimer_->start(kHoldMs);
}

void KeyBubble::placeAbove(const QRect& petRect) {
    QScreen* screen = QApplication::screenAt(petRect.center());
    if (!screen) screen = QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int y = petRect.top() - height() - 6;
    if (y < avail.top()) y = petRect.bottom() + 6;   // 上方放不下就翻到下面
    int x = petRect.center().x() - width() / 2;

    x = std::clamp(x, avail.left() + 4, avail.right() - width() - 4);
    y = std::clamp(y, avail.top() + 4, avail.bottom() - height() - 4);
    move(x, y);
}

void KeyBubble::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect body(0, 0, width(), height() - kTailH);

    // 圆角矩形和小尾巴先各自成形再取并集 —— 直接叠两个子路径描边会把尾巴根部那条线也画出来
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(QRectF(body).adjusted(1.5, 1.5, -1.5, -1.5), kRadius, kRadius);

    const qreal cx = width() / 2.0;
    const qreal base = body.bottom() - 2.0;
    QPainterPath tailPath;
    tailPath.moveTo(cx - 7.0, base);
    tailPath.lineTo(cx, base + kTailH + 1.0);
    tailPath.lineTo(cx + 7.0, base);
    tailPath.closeSubpath();

    p.setPen(QPen(QColor(0x9C, 0xC6, 0xF2), 2.0));    // 淡蓝描边
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, 0xF5));       // 略带透明的白
    p.drawPath(bodyPath.united(tailPath));

    p.setPen(QColor(0x39, 0x3A, 0x45));
    p.drawText(body, Qt::AlignCenter, text_);
}

} // namespace pic2pet
