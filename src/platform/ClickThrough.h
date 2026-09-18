#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

namespace pic2pet {

enum class ClickThroughBackend {
    Qt,      // QWidget::setWindowFlag(Qt::WindowTransparentForInput)
    Native,  // 平台原生：Windows WS_EX_TRANSPARENT / macOS setIgnoresMouseEvents / X11 XShape
};

/**
 * 鼠标穿透开关。
 *
 * 桌宠窗口绝大部分是透明的，透明区域必须让点击穿透到下面的窗口，
 * 只有角色本体区域接收鼠标。因此需要「按光标位置动态切换穿透」。
 *
 * 风险点：Qt 通用实现靠 QWidget::setWindowFlag()，而该 API 会隐藏并重建窗口，
 * 高频切换可能造成闪烁 / 焦点丢失。M0 spike 就是在实测这一点，
 * 因此同时提供原生实现以便对照（右键菜单可实时切换）。
 */
class IClickThrough : public QObject {
    Q_OBJECT
public:
    explicit IClickThrough(QWidget* window) : QObject(window), window_(window) {}

    virtual QString name() const = 0;
    virtual bool supported() const { return true; }

    bool currentValue() const { return current_; }
    int toggleCount() const { return toggleCount_; }

public slots:
    void setTransparent(bool on) {
        if (on == current_) return;          // 只在状态变化时调用平台接口
        if (!supported()) return;
        current_ = on;
        ++toggleCount_;
        apply(on);
    }

protected:
    virtual void apply(bool on) = 0;
    QWidget* window() const { return window_.data(); }

private:
    QPointer<QWidget> window_;
    bool current_ = false;
    int toggleCount_ = 0;
};

/** Qt 通用实现：跨平台，代价是切换时会隐藏并重建窗口 */
class QtClickThrough : public IClickThrough {
    Q_OBJECT
public:
    explicit QtClickThrough(QWidget* window) : IClickThrough(window) {}
    QString name() const override;
    bool supported() const override { return true; }
protected:
    void apply(bool on) override;
};

/** Windows 原生实现：直接改 WS_EX_TRANSPARENT，不触发窗口重建 */
class Win32ClickThrough : public IClickThrough {
    Q_OBJECT
public:
    explicit Win32ClickThrough(QWidget* window) : IClickThrough(window) {}
    QString name() const override;
    bool supported() const override;
protected:
    void apply(bool on) override;
};

IClickThrough* createClickThrough(ClickThroughBackend backend, QWidget* window);

} // namespace pic2pet
