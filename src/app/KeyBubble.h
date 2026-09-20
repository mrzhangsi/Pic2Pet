#pragma once

#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace pic2pet {

/**
 * "按键气泡"：敲键时在桌宠上方冒一个小圆角气泡，显示刚按下的那个键。
 *
 * 刻意做成**独立的小窗口**，而不是画在桌宠窗口里：桌宠窗口是
 * "无边框 + 透明 + OpenGL + 置顶 + 鼠标穿透"的组合，往里塞子控件容易和 OpenGL 合成打架；
 * 独立窗口用 QPainter 画圆角/小尾巴也更好控样式。
 *
 * 用 `Qt::WindowTransparentForInput` 让它**不吃鼠标**，不会挡住底下的窗口；
 * `WA_ShowWithoutActivating` 保证它不会把焦点从你正在打字的窗口抢走。
 */
class KeyBubble : public QWidget {
    Q_OBJECT
public:
    explicit KeyBubble(QWidget* parent = nullptr);

    /** 显示一个按键；停留一会儿后自动淡出。重复调用会刷新计时并更新文字。 */
    void showKey(quint32 vk);

    /** 把气泡摆到 petRect 正上方居中；顶到屏幕外就翻到下方，并夹在屏幕内。 */
    void placeAbove(const QRect& petRect);

    /** vk → 可读按键名（字母/数字直接给字符；其余给"空格""回车""↑"这类）。 */
    static QString keyLabel(quint32 vk);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    QTimer* holdTimer_ = nullptr;
    QPropertyAnimation* fade_ = nullptr;
};

} // namespace pic2pet
