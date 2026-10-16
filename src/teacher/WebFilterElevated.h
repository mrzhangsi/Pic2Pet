#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QThread;

namespace pic2pet {

/**
 * 用 UAC 提权重启自身，去写（或清）Edge 的机器级策略，然后回到结果。
 *
 * ## 为什么要这么绕
 *
 * Edge 策略只能写在 HKLM（原因见 WebFilter.h 的详细说明：HKCU\Software\Policies
 * 出厂默认只读），写 HKLM 必须提权，而桌宠是常驻托盘程序、不可能自己就是管理员。
 * 于是：需要写策略时，用 ShellExecuteEx + "runas" **重启自己一遍**，
 * 带 --webfilter-apply / --webfilter-clear 参数；那个子进程是提权的，
 * 干完活把结果写进临时文件就退出；父进程读结果、更新 UI。
 *
 * 为什么不引一个独立的小 helper exe：那会让 dist 多一个文件、多一套部署，
 * 而"重启自己"只需要 main.cpp 里一个早期分支。
 *
 * ## 约定
 *   父 → 子：命令行参数（两个参数都带同一个 job 文件路径）
 *   子 → 父：<jobPath>.result 文本文件，"ok" 或 "err: <原因>"
 *
 * ## 同步还是异步
 *   applyAsync/clearAsync —— 放到工作线程，绝不阻塞 GUI（UAC 可能挂十几秒）。
 *   applyBlocking/clearBlocking —— 退出 Pic2Pet 时用：那时已经没有事件循环了，
 *   必须同步等结果，否则进程先死、子进程还在写策略，留个半截状态。
 *
 * 两种接口都幂等安全：g_busy 保证同时只有一个提权任务在飞。
 */
class WebFilterElevated {
public:
    /**
     * 提权写入白名单。会弹 UAC（用户点"是"才继续）。
     * @param entries 白名单条目
     * @param error   失败原因（UAC 被取消 / 非管理员 / 超时 / 子进程报告失败…）
     */
    static bool applyBlocking(const QStringList& entries, QString* error);

    /** 提权清除策略。同步阻塞等待，同样会弹 UAC */
    static bool clearBlocking(QString* error);

    /** 异步版；done 在 GUI 线程被调用。用于开关点击等交互路径 */
    static void applyAsync(const QStringList& entries,
                           std::function<void(bool, const QString&)> done);
    static void clearAsync(std::function<void(bool, const QString&)> done);

    /**
     * 子进程入口：处理完就退出，不进入正常桌宠流程。
     * 在 main() 里紧跟 QApplication 构造之后调用，返回 true 表示"该退出了"。
     *
     * @param jobPath 命令行里跟的参数路径（apply 从它读白名单，clear 只用它推导 .result）
     * @param isApply true=写入白名单，false=清除
     */
    static bool runChildJob(const QString& jobPath, bool isApply);

    /** 命令行参数名（main.cpp 注册选项时也用得到） */
    static constexpr const char* kApplyArg = "--webfilter-apply";
    static constexpr const char* kClearArg = "--webfilter-clear";

private:
    static void runAsync(const QStringList& entries, bool isApply,
                         std::function<void(bool, const QString&)> done);
};

} // namespace pic2pet
