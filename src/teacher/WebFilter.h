#pragma once

#include <QString>
#include <QStringList>

namespace pic2pet {

/**
 * Edge 网站白名单（写 Edge 浏览器策略注册表，**不 hook、不注入、不杀进程**）。
 *
 * 实现方式 = 微软官方支持的 Edge 策略：
 *   HKCU\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist\1 = "*"        （拦截一切）
 *   HKCU\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist\1..N = 条目     （放行例外）
 * URLAllowlist 优先于 URLBlocklist，二者交集之外一律拦——正是「只允许白名单」。
 * 策略格式与匹配语义（前缀路径、子域、scheme）见 TeacherConfig.h 的 WebFilterCfg 注释，
 * 来源：Microsoft Learn「Filter formats for URL list-based policies」。
 *
 * 为什么选这条路（对比过的方案）：
 *   - hosts 文件：只能按域名拦、要管理员、且「只允许」写不出来（黑名单语义）
 *   - 本地代理 + 根证书：HTTPS 要 MITM，动静太大，违反「不为功能动架构」
 *   - WFP 驱动：要签名，发布直接完蛋
 *   - 监控 Edge 窗口标题再杀进程：标题滞后于导航，拦不住，体验也差
 *   - **Edge 策略**：官方机制、用户级免管理员、动态刷新、Edge 原生拦页，
 *     且我们只写两个键值，清除时原样删掉，零残留
 *
 * 生命周期由调用方（PetController）掌握：开启 = apply()，关闭/退出 = clear()。
 * clear() **只删自己写的编号值与因此变空的子键**，绝不碰用户/组织预先存在的值。
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
     * 成功返回 true；失败时 error() 有可展示给用户的原因。
     */
    bool apply(const QStringList& entries);

    /** 清除我们写的策略值；恢复 Edge 自由访问。幂等，可反复调用 */
    void clear();

    /**
     * 机器级（HKLM）是否已有同名策略——组织/学校下发的策略会覆盖用户级写入，
     * 且不是我们的东西、不能删。命中时 apply() 直接失败并提示。
     */
    static bool hasMachinePolicyConflict();

    /** 最近一次操作的错误原因（成功时为空） */
    const QString& error() const { return error_; }

    /** 当前策略是否正挂着（我们写的值还在）——用于 UI 状态与自愈校验 */
    static bool isActive();

private:
    QString error_;
};

} // namespace pic2pet
