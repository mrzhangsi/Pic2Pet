#include "ClickThrough.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace pic2pet {

// ---------- Qt 通用实现 ----------

QString QtClickThrough::name() const { return QStringLiteral("Qt::WindowTransparentForInput"); }

void QtClickThrough::apply(bool on) {
    QWidget* w = window();
    if (!w) return;

    // 坑：QWidget::setWindowFlag() 会隐藏窗口，必须手动恢复显示。
    // M0 需要观察：恢复显示是否引起闪烁、是否打断 OpenGL 渲染、是否抢焦点。
    const bool wasVisible = w->isVisible();
    w->setWindowFlag(Qt::WindowTransparentForInput, on);
    if (wasVisible) w->show();
}

// ---------- Windows 原生实现 ----------

QString Win32ClickThrough::name() const { return QStringLiteral("WS_EX_TRANSPARENT (Win32)"); }

bool Win32ClickThrough::supported() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void Win32ClickThrough::apply(bool on) {
    Q_UNUSED(on);
#ifdef Q_OS_WIN
    QWidget* w = window();
    if (!w) return;

    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return;

    const LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    const LONG_PTR next = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    if (next == ex) return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE, next);
    // SWP_FRAMECHANGED 强制窗口管理器立刻应用新样式，否则部分场景要等下次重绘才生效
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

IClickThrough* createClickThrough(ClickThroughBackend backend, QWidget* window) {
    if (backend == ClickThroughBackend::Native) {
        auto* native = new Win32ClickThrough(window);
        if (native->supported()) return native;
        delete native;   // 非 Windows 平台退回 Qt 实现
    }
    return new QtClickThrough(window);
}

} // namespace pic2pet
