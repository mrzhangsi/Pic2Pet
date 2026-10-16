#pragma once

#include <QRect>
#include <QWidget>

class QLabel;
class QPushButton;

namespace pic2pet {

/**
 * "访问了白名单之外的网站"警告窗。
 *
 * ## 定位：提醒，不是防护
 *
 * 这个窗**拦不住任何东西**。它只是在你访问到不在白名单里的网站时冒出来提醒一声，
 * 点掉就能接着访问。它的价值是"专注时的自我提醒"，不是家长控制 ——
 * 所以文案里不写"已阻止"，写的是"检测到"。
 *
 * ## 为什么不做成模态对话框
 *
 * 触发的时候你多半正在浏览器里操作（甚至在打字）。模态会：
 *   - 抢走焦点 → 你打的字跑到弹窗里去
 *   - 阻塞输入 → 反而打断"进入状态"
 * 所以这里是**非模态的独立小窗**：
 *   `Qt::WindowDoesNotAcceptFocus` + `WA_ShowWithoutActivating`
 * 保证它浮在最上层、看得见、按钮也能点，但**不抢焦点**。
 * 和 KeyBubble 同一个思路，只是它需要能点，所以不加 `WindowTransparentForInput`。
 *
 * ## 位置
 *
 * 停在**屏幕右上角**（任务栏托盘附近），不跟着桌宠跑：
 * 桌宠经常被用户拖到屏幕中间或者藏在窗口后面，而警告是"系统级"的提示，
 * 固定在角落更稳定、也不会因为桌宠移动而抖动。
 */
class EdgeAlert : public QWidget {
    Q_OBJECT
public:
    explicit EdgeAlert(QWidget* parent = nullptr);

    /**
     * 显示一条警告。已在显示时会就地更新文字并重新计时，不会叠出第二个窗。
     * @param site    越界站点（域名）
     * @param entries 生效中的白名单，用于提示"允许的都有哪些"
     */
    void showViolation(const QString& site, const QStringList& entries);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /** 重新按文字算尺寸并摆到右上角（宽高会随白名单条目数变化） */
    void relayout();

    QLabel* titleLabel_ = nullptr;
    QLabel* siteLabel_ = nullptr;
    QLabel* hintLabel_ = nullptr;
    QLabel* listLabel_ = nullptr;
    QPushButton* closeButton_ = nullptr;
};

} // namespace pic2pet
