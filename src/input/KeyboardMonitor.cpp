#include "KeyboardMonitor.h"

#include <QMetaObject>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace pic2pet {

#ifdef Q_OS_WIN

namespace {

// 钩子回调是文件级静态函数，拿不到 this，用这个指针回指当前活跃实例
KeyboardMonitor* gInstance = nullptr;

LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && gInstance) {
        const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (k) {
            const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            // 只关心按下/抬起。注意：这里**只取 vkCode，绝不读取/记录任何按键内容**
            if (down || up) gInstance->postKeyEvent(static_cast<quint32>(k->vkCode), down);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace

bool KeyboardMonitor::startWin32(QString* error) {
    // WH_KEYBOARD_LL 是低层钩子：hMod 必须为 nullptr，dwThreadId = 0 表示全局
    const HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
    if (!h) {
        if (error) {
            *error = QStringLiteral("安装键盘钩子失败（错误码 %1）").arg(quint32(GetLastError()));
        }
        return false;
    }
    hook_ = reinterpret_cast<void*>(h);
    gInstance = this;
    return true;
}

void KeyboardMonitor::stopWin32() {
    if (hook_) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(hook_));
        hook_ = nullptr;
    }
    if (gInstance == this) gInstance = nullptr;
}

bool KeyboardMonitor::start(QString* error) {
    if (running_) return true;
    if (!startWin32(error)) return false;
    running_ = true;
    return true;
}

#else

bool KeyboardMonitor::start(QString* error) {
    if (error) *error = QStringLiteral("当前平台不支持全局键盘监听");
    return false;
}

#endif

KeyboardMonitor::KeyboardMonitor(QObject* parent) : QObject(parent) {}

KeyboardMonitor::~KeyboardMonitor() { stop(); }

void KeyboardMonitor::stop() {
    if (!running_) return;
#ifdef Q_OS_WIN
    stopWin32();
#endif
    running_ = false;
}

void KeyboardMonitor::postKeyEvent(quint32 vk, bool down) {
    // 钩子回调发生在消息泵内部；这里不直接 emit，而是投递给事件循环，
    // 避免在回调里执行业务逻辑（也避免重入）。
    QMetaObject::invokeMethod(
        this, [this, vk, down] { emit keyEvent(vk, down); }, Qt::QueuedConnection);
}

QString KeyboardMonitor::name() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Win32 WH_KEYBOARD_LL");
#else
    return QStringLiteral("none (platform stub)");
#endif
}

} // namespace pic2pet
