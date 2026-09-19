#pragma once

#include <QObject>
#include <QString>

namespace pic2pet {

/**
 * 全局键盘监听 —— "输入模式"的底座：你敲键盘时，桌宠也能有反应。
 *
 * ⚠️ 这是本项目**唯一一个挂钩式**的能力，和"鼠标穿透"（只是个窗口样式）完全不是一个量级：
 *   - 低层键盘钩子会看到所有按键，所以**默认关闭**，必须用户显式打开才装；
 *   - 只把按键的**虚拟键码（VK）**交给上层，**不记录、不落盘、也不打印日志**；
 *   - 会明显加重 Defender / SmartScreen 的误报（见 README 的「被报毒」一节）。
 *
 * 实现：Windows 走 SetWindowsHookEx(WH_KEYBOARD_LL)；其它平台是空实现（保证三平台能编译）。
 * 低层钩子要求安装它的线程有消息循环，Qt 主线程满足 —— 所以 start() 只能在主线程调用。
 */
class KeyboardMonitor : public QObject {
    Q_OBJECT
public:
    explicit KeyboardMonitor(QObject* parent = nullptr);
    ~KeyboardMonitor() override;

    /** 安装钩子。失败时把原因写进 *error（可为 nullptr）。 */
    bool start(QString* error = nullptr);

    /** 卸载钩子 */
    void stop();

    bool running() const { return running_; }

    /** 实现名，仅用于日志 */
    QString name() const;

    /**
     * **内部用**：由钩子回调调用，把按键投递到事件循环。
     * 不要手动调用它。做成 public 只是因为钩子是文件级静态函数。
     */
    void postKeyEvent(quint32 vk, bool down);

signals:
    /** down = true 为按下，false 为抬起。vk 是 Windows 虚拟键码。 */
    void keyEvent(quint32 vk, bool down);

private:
#ifdef Q_OS_WIN
    bool startWin32(QString* error);
    void stopWin32();
#endif

    bool running_ = false;
    void* hook_ = nullptr;   // HHOOK；这里不放 windows.h，保持头文件干净
};

} // namespace pic2pet
