#pragma once

#include <QString>
#include <QStringList>

namespace pic2pet {

/**
 * Edge 网站白名单（写 Edge 浏览器**机器级**策略注册表，**不 hook、不注入、不杀进程**）。
 *
 * 实现方式 = 微软官方支持的 Edge 策略：
 *   HKLM\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist\1 = "*"        （拦截一切）
 *   HKLM\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist\1..N = 条目     （放行例外）
 * URLAllowlist 优先于 URLBlocklist，二者交集之外一律拦——正是「只允许白名单」。
 * 策略格式与匹配语义（前缀路径、子域、scheme）见 TeacherConfig.h 的 WebFilterCfg 注释，
 * 来源：Microsoft Learn「Filter formats for URL list-based policies」。
 *
 * ## 为什么是 HKLM，而且必须提权
 *
 * 最自然的写法是 HKCU\SOFTWARE\Policies\Microsoft\Edge（用户级、看着像免管理员），
 * **但那条路走不通**：HKCU\Software\Policies 这个键 Windows 出厂默认就把普通用户降成只读
 * （DACL 禁用继承、只留 SYSTEM/Administrators 可写，实测 inherited=False 的显式 ACE）。
 * 微软文档也明说 "Users can not write to the following sections of HKCU: \Software\Policies"，
 * 设计意图就是防止用户自行解除组策略。
 * 而 Chromium 在 Windows 上**只有注册表一个策略入口**（Linux 才有 /etc/opt/edge/policies/managed/），
 * 所以没有"换个可写位置"的第二条路 —— 只剩 HKLM + UAC 提权。
 *
 * 副作用要知道：HKLM 是**机器级**，本机所有 Windows 账户的 Edge 都会被一起限制。
 *
 * ## 提权怎么做的
 *
 * 本文件只做注册表读写，**不碰 UAC**。提权由调用方（PetController）用
 * ShellExecuteEx + "runas" 重启自身、带 --webfilter-apply/--webfilter-clear 参数完成，
 * 子进程写完（结果写进临时文件）即退出。apply()/clear() 都会检查 isElevated()，
 * 没提权就调用会直接失败而不是留下半截状态。
 *
 * ## 为什么选 Edge 策略（对比过的方案）
 *   - hosts 文件：只能按域名拦、要管理员、且「只允许」写不出来（黑名单语义）
 *   - 本地代理 + 根证书：HTTPS 要 MITM，动静太大，违反「不为功能动架构」
 *   - WFP 驱动：要签名，发布直接完蛋
 *   - 监控 Edge 窗口标题再杀进程：标题滞后于导航，拦不住，体验也差
 *
 * 生命周期由调用方（PetController）掌握：开启 = apply()，关闭/退出 = clear()。
 * clear() **只删自己写的编号值与因此变空的子键**，绝不碰用户/组织预先存在的值
 * （有预存值时 apply() 先备份到 kBackupRoot，clear() 原样写回）。
 *
 * 只实现 Windows；其他平台 isSupported() 为 false，apply() 失败（保证能编译）。
 */
class WebFilter {
public:
    WebFilter() = default;

    static bool isSupported();

    /**
     * 应用白名单：URLBlocklist 写 "*"，URLAllowlist 写 entries。
     * entries 为空 = 拒绝（防止把 Edge 全关死，见 UI 层同样的约束）。
     * **必须在已提权的进程里调用**（否则直接失败）。
     * 成功返回 true；失败时 error() 有可展示给用户的原因。
     */
    bool apply(const QStringList& entries);

    /** 清除我们写的策略值；恢复 Edge 自由访问。幂等，可反复调用。同样要求已提权。 */
    void clear();

    /**
     * 策略键里有没有**不是我们写的**的值（组织/学校下发的，或用户自己配的）。
     * 命中时 apply() 会先备份它们，clear() 再原样写回。
     * owned 标记在备份键里：说明当前值是我们上次写的，不算预存值。
     *
     * 注意：这不是"检测到组织策略就拒绝"—— 那是 HKCU 时代的逻辑，
     * 改成 HKLM 后我们自己写的值也会命中，判定条件必须换成"不是我写的"。
     */
    static bool hasForeignPolicyValues();

    /** 当前进程是否已提权（写 HKLM 的前提）。管理员账户在未提权进程里也返回 false。 */
    static bool isElevated();

    /** 最近一次操作的错误原因（成功时为空） */
    const QString& error() const { return error_; }

    /** 当前策略是否正挂着（我们写的值还在）——用于 UI 状态与自愈校验。只读，不需要提权。 */
    static bool isActive();

private:
    QString error_;
};

} // namespace pic2pet
