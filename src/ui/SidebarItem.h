#pragma once

#include <QAbstractButton>
#include <QColor>

#include "Icons.h"

namespace pic2pet {

/**
 * 侧边栏的一项：图标 + 文字，选中态是圆角蓝底 + 白字白图标（macOS 系统设置那样）。
 * 同样自绘 —— QPushButton 的 :checked 换不了图标颜色。
 */
class SidebarItem : public QAbstractButton {
    Q_OBJECT

public:
    SidebarItem(theme::Icon icon, const QColor& iconColor, const QString& text,
                QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    theme::Icon icon_;
    QColor iconColor_;
    bool hover_ = false;
};

} // namespace pic2pet
