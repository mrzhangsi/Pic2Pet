#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace pic2pet {

/**
 * Edge 地址栏监视器：**只读**当前访问的地址，越过白名单就报警。不拦截、不改系统。
 *
 * ## 为什么是"只读 + 弹窗"而不是"真拦截"
 *
 * 最初的实现写 Edge 官方策略（URLBlocklist/URLAllowlist）真拦截。实测走不通：
 * 策略只能写 HKLM（`HKCU\Software\Policies` 出厂默认只读），于是每次开关都要弹 UAC、
 * 且是**机器级**限制（本机所有账户）、还会让 Edge 挂上 "Managed by your organization"。
 * 用户决定改为**只检测 + 弹窗警告** —— 彻底不碰系统：无需 UAC、零残留、不影响别的浏览器。
 *
 * 代价要说清楚：**这是提醒，不是防护。** 关掉警告框就能继续访问，
 * 用户手动改注册表也能绕过（不过现在压根不写注册表了）。
 * 它的定位是"专注时的自我提醒"，不是"家长控制"。
 *
 * ## 怎么读到地址：UI Automation
 *
 * Edge 的**窗口标题拿不到 URL**。实测标题是：
 *   "54. 基础-事务-并发事务问题_哔哩哔哩_bilibili 和另外 1 个页面 - 个人 - Microsoft Edge"
 * 里面的 "bilibili" 是**页面 `<title>` 自带的**，站点不一定会写，标题末尾也不含域名。
 * 所以标题方案不可靠。
 *
 * 唯一能拿到完整地址的办法是 **UI Automation** 读地址栏控件。实测可行：
 *   - 窗口类名 `Chrome_WidgetWin_1`
 *   - 地址栏 ClassName=`OmniboxViewViews`，AutomationId=`view_1017`（**随版本变，别硬编码**）
 *   - 用 ValuePattern 取 CurrentValue → 完整 URL
 * 实测取到：`https://www.bilibili.com/video/BV1Kr4y1i7ru/?spm_id_from=...&p=54`
 *
 * 按 ClassName 定位，不用 AutomationId —— 后者是版本相关的实现细节。
 *
 * ## 读不到就当没越界（宁可漏报不误报）
 *
 * UIA 读不到的情况真实存在：UAC 提权桌面、全屏视频、Edge 内部页、浏览器启动/关闭中。
 * 这些一律**不报警** —— 一个老弹的警告比漏报更招人烦。
 *
 * ## 线程
 *
 * UIA 的 COM 对象**不是线程安全的**，`CoInitializeEx` 也必须在使用它的线程上。
 * 所以轮询跑在**独立线程**里（自带消息循环），结果通过信号回 GUI 线程。
 */
class EdgeWatch : public QObject {
    Q_OBJECT
public:
    explicit EdgeWatch(QObject* parent = nullptr);
    ~EdgeWatch() override;

    /**
     * 开始监视。entries 是白名单快照（**按值传递**）：
     * 轮询线程不该在运行中读 Settings，用户中途改白名单也不该影响正在跑的判定
     * （要生效就 stop + start）。
     * 空名单 = 不启动（没有可判定的基准，只会让所有站点都报警）。
     */
    void start(const QStringList& entries);

    /** 停止监视。必须等待线程结束 —— UIA 对象在该线程里 Release */
    void stop();

    bool running() const { return thread_ != nullptr; }

    /**
     * 判断 url 是否落在白名单内。
     *
     * 匹配语义沿用 Edge 官方 URL 过滤器（用户配条目的直觉不变）：
     *   - 条目不含 '/' → 只比主机，且**含全部子域**（`bilibili.com` 匹配 `www.bilibili.com`）
     *   - 条目含 '/'   → 主机 + 路径都要匹配，路径按**前缀**且**按路径段**
     *                    （`example.com/math` 放行 `/math`、`/math/x`，不放行 `/mathx`）
     *   - 前导点 = 精确该主机（`.example.com` 不含子域）
     *   - 条目可带 scheme / 端口，会被剥掉
     *   - `edge://` / `about:` / `file://` 这类内部页**永远放行**（不是"上网"）
     *   - 空名单 / 解析不出主机 → 放行，绝不误报
     */
    static bool urlAllowed(const QString& url, const QStringList& entries);

    /** 从 url 里取出用于展示的站点名（域名；内部页则返回其 scheme） */
    static QString displaySite(const QString& url);

signals:
    /**
     * 检测到访问白名单外的网站。**同一站点只发一次**，回到白名单内后才重置。
     * @param site    展示用站点名（域名）
     * @param entries 本次判定用的白名单（供弹窗提示）
     */
    void violationDetected(const QString& site, const QStringList& entries);

private:
    class QThread* thread_ = nullptr;
};

} // namespace pic2pet
