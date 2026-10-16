# 网站白名单检测（src/monitor）

只**读** Edge 当前访问的地址，发现不在白名单就**弹窗提醒**。
**不拦截、不注入、不 hook、不改任何系统设置。**

## ⚠️ 这是提醒，不是防护

关掉警告框就能继续访问。它是"专注时的自我提醒"，不是家长控制。
想要真拦截只能靠浏览器策略，而那需要管理员权限改注册表 —— 已放弃（见下）。

## 为什么不做真拦截

最初的实现写 Edge 官方 URL 策略（`URLBlocklist` + `URLAllowlist`）。实测走不通：

- 策略只能写 `HKLM`（`HKCU\Software\Policies` **出厂默认只读**，只有 SYSTEM/Administrators
  能写，微软文档说明这是为了防止用户自行解除组策略）
- → 每次开关都要弹 UAC，且是**机器级**限制（本机所有账户的 Edge 一起被限）
- → Edge 还会挂上 "Managed by your organization"

用户决定：不要动系统，改成只检测 + 弹窗。

## 怎么读到当前地址（这里有个坑）

**窗口标题拿不到 URL。** 实测 Edge 标题是：

```
54. 基础-事务-并发事务问题_哔哩哔哩_bilibili 和另外 1 个页面 - 个人 - Microsoft Edge
```

里面的 `bilibili` 是**页面 `<title>` 自带的** —— 站点不一定会把域名写进 title，
标题末尾也不含域名。所以标题方案不可靠。

**只能用 UI Automation 读地址栏控件**（`EdgeWatch.cpp`）：

| 项 | 值 |
|---|---|
| 窗口类名 | `Chrome_WidgetWin_1` |
| **定位条件** | **`ControlType == Edit`**（UIA_EditControlTypeId） |
| 地址栏 ClassName | `OmniboxViewViews`（只用来**校验**，不用来定位） |
| 取值 | `IUIAutomationValuePattern::get_CurrentValue` |

### ⚠️ 坑：不能用 ClassName 做查找条件

第一次实现用 `CreatePropertyCondition(UIA_ClassNamePropertyId, "OmniboxViewViews")`
+ `FindFirst` 去定位，实测**永远读不到地址**（枚举到了 4 个 Edge 窗口，地址数恒为 0）。

原因：UIA 的 ClassName 是**计算属性**（由 provider 现算出来），拿它当条件做树查找，
Chromium 的 provider 不支持这种匹配 —— `FindFirst` 返回 `S_OK` 但结果是空指针，
不报错、最难查。

改成 `ControlType == Edit` 就一次命中 —— ControlType 是 UIA 的**原生缓存属性**，
所有 provider 必须支持。（`temp/uia_probe/` 那个验证程序一开始就是用 ControlType，
所以它能成；换成 ClassName 才复现了问题。）

因为 Edge 里不止一个 Edit（搜索框等），所以：**用 ControlType 定位 → 读 ClassName 校验**
是不是 `OmniboxViewViews`。第一个 Edit 不是地址栏时会退化成 `FindAll` 全量扫。

顺带两个 API 名容易写错的：
- `FindAll` 返回的是 **`IUIAutomationElementArray`**，取值方法叫 `GetElement(i, &e)`；
  `get_CurrentLength` / `GetCurrentElementAt` 挂在 `IUIAutomationElement` 上，编译不过。

## 行为约定

- 轮询间隔 1 秒；**页面没变化就不做事**，避免重复判定
- **同一站点只弹一次**，回到白名单内才重置 —— 否则在一堆标签页间切换会弹疯
- **读不到就当没越界**（UAC 桌面、全屏视频、内部页、浏览器启动中）——
  宁可漏报不误报，一个老弹的警告比漏报更招人烦
- **多窗口**：枚举所有可见 Edge 窗口，取第一个读得到地址的
- 只管 Edge，不管其它浏览器
- 诊断日志只在**「枚举到窗口却读不到地址」连续 3 轮**时打一条 warning ——
  "没有 Edge 窗口"是正常状态，不刷屏

## 白名单匹配

沿用 Edge 官方 URL 过滤器语义（用户配条目的直觉不变）：

| 条目 | 匹配 |
|---|---|
| `bilibili.com` | 该主机 + 全部子域 |
| `.example.com` | 仅该主机，不含子域 |
| `example.com/math` | 主机 + 路径前缀，且**按路径段**（放行 `/math`、`/math/x`，不放行 `/mathx`） |

内部页（`edge://`、`about:`、`file://`、`data:` …）永远放行 —— 那不是上网。
空名单 → 不启动监视。

## 线程

UIA 的 COM 对象**不是线程安全的**，`CoInitializeEx` 必须在使用它的线程上。
轮询跑在独立线程（`EdgeWatchThread`）自带消息循环，结果通过信号回 GUI 线程。
`stop()` 里的 `wait()` 不能省 —— 否则 COM 对象还没 `Release` 就被析构。

## 实测记录

- 匹配逻辑：`temp/edgewatch/test_urlmatch.py`，29 条用例（含 `/math` 不放行 `/mathx`
  这类前缀边界），全通过
- 端到端：`temp/edgewatch/run_e2e.py`（写配置 → 启动 → 打开指定站点 → 全屏截图 →
  还原用户原配置）
  - 白名单 `example.com`，访问 github.com → 报警，弹窗显示 `github.com` ✅
  - 白名单 `github.com,bilibili.com,example.com`，访问 github.com → **不报警** ✅
