#include "EdgeAlert.h"

#include "Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace pic2pet {

namespace {

constexpr int kPadX = 22;          // 卡片左右留白
constexpr int kPadY = 18;          // 卡片上下留白
constexpr int kGap = 6;            // 标签之间行距
constexpr qreal kRadius = 14.0;    // 圆角
constexpr int kMargin = 16;        // 距屏幕边缘
constexpr int kMaxListLines = 6;   // 白名单最多列几行，超出折叠

} // namespace

EdgeAlert::EdgeAlert(QWidget* parent) : QWidget(parent) {
    // 非模态 + 不抢焦点：警告多半在你正操作浏览器时弹出来，抢焦点会把输入打断。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setWindowTitle(QStringLiteral("网站白名单提醒"));

    titleLabel_ = new QLabel(this);
    titleLabel_->setText(QStringLiteral("检测到白名单之外的网站"));
    QFont tf = titleLabel_->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() - 0.5);
    titleLabel_->setFont(tf);
    titleLabel_->setStyleSheet(
        QStringLiteral("color:%1; background:transparent;").arg(theme::TextPrimary.name()));

    siteLabel_ = new QLabel(this);
    QFont sf = siteLabel_->font();
    sf.setBold(true);
    sf.setPointSizeF(sf.pointSizeF() + 5.0);
    siteLabel_->setFont(sf);
    siteLabel_->setStyleSheet(
        QStringLiteral("color:%1; background:transparent;").arg(theme::Accent.name()));
    siteLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    hintLabel_ = new QLabel(this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(
        QStringLiteral("color:%1; background:transparent;").arg(theme::TextSecond.name()));

    listLabel_ = new QLabel(this);
    listLabel_->setWordWrap(true);
    listLabel_->setStyleSheet(
        QStringLiteral("color:%1; background:transparent;").arg(theme::TextSecond.name()));

    closeButton_ = new QPushButton(QStringLiteral("知道了"), this);
    closeButton_->setCursor(Qt::PointingHandCursor);
    closeButton_->setFixedHeight(30);
    closeButton_->setMinimumWidth(96);
    closeButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:#FFFFFF;border:none;border-radius:8px;"
        "font-weight:600;}"
        "QPushButton:hover{background:%2;}"
        "QPushButton:pressed{background:#0062CC;}")
        .arg(theme::Accent.name(), theme::Accent.darker(112).name()));
    connect(closeButton_, &QPushButton::clicked, this, &QWidget::hide);

    auto* col = new QVBoxLayout;
    col->setContentsMargins(kPadX, kPadY, kPadX, kPadY);
    col->setSpacing(kGap);
    col->addWidget(titleLabel_);
    col->addWidget(siteLabel_);
    col->addWidget(hintLabel_);
    col->addWidget(listLabel_);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 4, 0, 0);
    row->addStretch(1);
    row->addWidget(closeButton_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addLayout(col);
    root->addLayout(row);
}

void EdgeAlert::showViolation(const QString& site, const QStringList& entries) {
    siteLabel_->setText(site);
    // 措辞刻意用"提醒"而不是"已阻止" —— 这东西真的拦不住，别给人虚假的安全感。
    hintLabel_->setText(QStringLiteral(
        "这个网站不在白名单里。Pic2Pet 只是提醒你一下，关闭这个窗口就能继续访问。"));

    if (entries.isEmpty()) {
        listLabel_->setText(QStringLiteral("当前白名单是空的。"));
    } else {
        QStringList shown = entries;
        QString tail;
        if (shown.size() > kMaxListLines) {
            tail = QStringLiteral("…… 等共 %1 个").arg(shown.size());
            shown = shown.mid(0, kMaxListLines);
        }
        listLabel_->setText(
            QStringLiteral("白名单（按前缀匹配）：%1%2")
                .arg(shown.join(QStringLiteral("、")), tail));
    }

    relayout();

    show();
    raise();
    closeButton_->setFocus();   // 焦点给按钮，但整窗 WindowDoesNotAcceptFocus，不抢系统焦点
}

void EdgeAlert::relayout() {
    const QFontMetrics fmSite(siteLabel_->font());
    const QFontMetrics fmTitle(titleLabel_->font());
    const QFontMetrics fmBody(hintLabel_->font());

    // 定宽 + 让布局算高度：QLabel 的 wordWrap 只有在"宽度已知"时才给得出正确高度，
    // 手算行数必然会在某些文字上错一行（实测会把提示语挤成一坨）。
    //
    // 宽度不能只按站点名算：域名短（github.com）而提示语长，只按域名定宽会得到
    // 一个极窄的窗，正文被压成十几行。所以**下限由正文宽度决定**，再和站点名取大。
    // setFixedWidth + adjustSize 让 Qt 自己按这个宽度算出正确高度。
    constexpr int kMaxTextW = 420;
    const int bodyW = fmBody.horizontalAdvance(hintLabel_->text());
    int w = std::max(fmSite.horizontalAdvance(siteLabel_->text()),
                     std::max(fmTitle.horizontalAdvance(titleLabel_->text()), bodyW));
    setFixedWidth(std::clamp(w + kPadX * 2, 260, kMaxTextW));

    layout()->activate();
    adjustSize();   // 高度由 layout 的 heightForWidth 决定

    QScreen* screen = QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    move(avail.right() - width() - kMargin, avail.top() + kMargin);
}

void EdgeAlert::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), kRadius, kRadius);

    p.setPen(QPen(theme::Border, 1.0));
    p.setBrush(theme::ContentBg);
    p.drawPath(path);
}

} // namespace pic2pet
