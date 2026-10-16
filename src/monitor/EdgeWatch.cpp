#include "EdgeWatch.h"

#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <algorithm>
#include <cwchar>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <uiautomation.h>
#include <oleauto.h>
#endif

namespace pic2pet {
namespace {

#ifdef Q_OS_WIN

/** Edge 主窗口类名（Chromium 系通用）。用它筛掉其它程序的窗口 */
constexpr const wchar_t* kEdgeWindowClass = L"Chrome_WidgetWin_1";

/** 轮询间隔。1 秒：够跟手（切页面立刻报警），又不会给 CPU 添麻烦（UIA 遍历不便宜） */
constexpr int kPollIntervalMs = 1000;

/** 地址栏控件的 ClassName。**刻意不用 AutomationId**（形如 view_1017，随版本变） */
constexpr const wchar_t* kOmniboxClass = L"OmniboxViewViews";

QString readOmniboxImpl(HWND hwnd, IUIAutomation* uia);   // 实现在下方

/**
 * Win32 枚举回调的上下文。
 * EnumWindows 的回调是普通函数指针（不能捕获 lambda），所以用这个 POD 传状态。
 */
struct EnumContext {
    IUIAutomation* uia = nullptr;
    QStringList* urls = nullptr;
    int* windows = nullptr;   // 诊断：枚举到几个 Edge 窗口（与"读到几个地址"区分开）
};

BOOL CALLBACK enumEdgeWindow(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    if (wcscmp(cls, kEdgeWindowClass) != 0) return TRUE;

    if (ctx->windows) ++(*ctx->windows);
    const QString url = readOmniboxImpl(hwnd, ctx->uia);
    if (!url.isEmpty()) ctx->urls->append(url);
    return TRUE;
}

#endif

} // namespace

// ---------------------------------------------------------------------------
// 匹配逻辑（纯函数，不依赖平台，便于单独验证）
// ---------------------------------------------------------------------------

namespace {

/** 内部页 / 非 http(s)：不是"上网"，永远放行 */
bool isInternalUrl(const QString& url) {
    static const QStringList kInternal = {
        QStringLiteral("edge://"),     QStringLiteral("about:"),
        QStringLiteral("chrome://"),   QStringLiteral("devtools://"),
        QStringLiteral("view-source:"), QStringLiteral("data:"),
        QStringLiteral("javascript:"), QStringLiteral("blob:"),
        QStringLiteral("file://"),
    };
    for (const QString& s : kInternal) {
        if (url.startsWith(s, Qt::CaseInsensitive)) return true;
    }
    return false;
}

/**
 * 把一条白名单条目拆成 (主机, 路径前缀)。
 * 允许用户带 scheme / 端口，都会被剥掉 —— 用户写 "https://bilibili.com" 也认。
 * 返回 false = 条目不合法（空）。
 */
bool parseEntry(const QString& raw, QString* host, QString* pathPrefix) {
    QString e = raw.trimmed().toLower();
    if (e.isEmpty()) return false;

    const int schemeEnd = e.indexOf(QLatin1String("://"));
    if (schemeEnd >= 0) e = e.mid(schemeEnd + 3);

    const int slash = e.indexOf(QLatin1Char('/'));
    QString h = (slash < 0) ? e : e.left(slash);
    *pathPrefix = (slash < 0) ? QString() : e.mid(slash);
    if (h.isEmpty()) return false;

    // 剥掉端口：host:8080
    const int colon = h.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) h = h.left(colon);

    *host = h;
    return true;
}

} // namespace

bool EdgeWatch::urlAllowed(const QString& url, const QStringList& entries) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return true;
    if (isInternalUrl(trimmed)) return true;
    // 空名单 = 不检查。宁可一次都不报，也不要"刚开就狂弹"。
    if (entries.isEmpty()) return true;

    const QUrl u(trimmed);
    // 解析不出主机（相对路径、畸形 URL）→ 放行，不误报
    if (!u.isValid() || u.host().isEmpty()) return true;

    const QString host = u.host().toLower();
    const QString path = u.path().toLower();

    for (const QString& raw : entries) {
        QString entryHost, entryPath;
        if (!parseEntry(raw, &entryHost, &entryPath)) continue;

        // 主机匹配，语义与 Edge 官方 URL 过滤器一致：
        //   以点开头 = 精确该主机（.example.com）
        //   不带点   = 含全部子域（bilibili.com 匹配 www.bilibili.com）
        const bool hostMatch = entryHost.startsWith(QLatin1Char('.'))
                                   ? host == entryHost.mid(1)
                                   : (host == entryHost
                                      || host.endsWith(QLatin1Char('.') + entryHost));
        if (!hostMatch) continue;

        // 条目没有路径部分 = 该主机下所有路径放行
        if (entryPath.isEmpty()) return true;

        // 有路径部分 = **前缀匹配**。example.com/math 放行 /math、/math/x，
        // 但不放行 /mathx（那不是 /math 下的东西）。
        const QString prefix = entryPath.endsWith(QLatin1Char('/'))
                                   ? entryPath
                                   : entryPath + QLatin1Char('/');
        if (path == entryPath || path.startsWith(prefix)) return true;
    }
    return false;
}

QString EdgeWatch::displaySite(const QString& url) {
    const QString trimmed = url.trimmed();
    for (const QString& s : {QStringLiteral("edge://"), QStringLiteral("about:"),
                             QStringLiteral("chrome://")}) {
        if (trimmed.startsWith(s, Qt::CaseInsensitive)) return s;
    }
    const QString host = QUrl(trimmed).host();
    if (!host.isEmpty()) return host;
    return trimmed.left(80);
}

#ifdef Q_OS_WIN

namespace {

/**
 * 从一个 Edge 窗口句柄读出地址栏文本。
 * 返回空串 = 读不到（内部页 / 全屏 / UAC 桌面 / 正在切换…），
 * 调用方据此判定"不报警"——宁可漏报不误报。
 *
 * ## 为什么按 ControlType 找、而不是按 ClassName 找
 *
 * 地址栏的 ClassName 是 `OmniboxViewViews`，看着最直接，但 UIA 的 ClassName
 * 属于**计算属性**（由 provider 现算），拿它当条件做 FindFirst 在 Chromium 的
 * provider 上匹配不上（实测：条件非空、FindFirst 返回 S_OK，但 found 指针为空）。
 * 而 ControlType=Edit 是 UIA 的**原生缓存属性**，所有 provider 都必须支持 ——
 * 临时验证程序正是用这个条件一次就读到了完整 URL。
 *
 * 所以：**用 ControlType 定位，再读出 ClassName 校验**是 OmniboxViewViews。
 * 只做 ControlType 匹配会命中搜索框等其他 Edit，用 ClassName 校验把这个排掉。
 */
QString readOmniboxImpl(HWND hwnd, IUIAutomation* uia) {
    if (!uia) return QString();

    IUIAutomationElement* root = nullptr;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) return QString();

    VARIANT vEdit{};
    vEdit.vt = VT_I4;
    vEdit.lVal = UIA_EditControlTypeId;

    IUIAutomationCondition* cond = nullptr;
    if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, vEdit, &cond))
        || !cond) {
        root->Release();
        return QString();
    }

    QString result;
    // Edge 地址栏不是唯一的 Edit（搜索框、未加载的地址栏等），
    // 所以把所有 Edit 扫一遍，取第一个 ClassName 对得上的。
    IUIAutomationElement* found = nullptr;
    if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &found)) && found) {
        // 已定位到一个 Edit：先看它是不是地址栏
        auto isOmnibox = [](IUIAutomationElement* e) {
            VARIANT v{};
            bool ok = false;
            if (SUCCEEDED(e->GetCurrentPropertyValue(UIA_ClassNamePropertyId, &v))
                && v.vt == VT_BSTR) {
                ok = (wcscmp(v.bstrVal, kOmniboxClass) == 0);
            }
            VariantClear(&v);
            return ok;
        };

        auto readValue = [](IUIAutomationElement* e) {
            QString s;
            IUIAutomationValuePattern* vp = nullptr;
            if (SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp)))
                && vp) {
                BSTR val = nullptr;
                if (SUCCEEDED(vp->get_CurrentValue(&val)) && val) {
                    s = QString::fromWCharArray(val, ::wcslen(val));
                    SysFreeString(val);
                }
                vp->Release();
            }
            return s;
        };

        if (isOmnibox(found)) {
            result = readValue(found);
        } else {
            // 第一个 Edit 不是地址栏（多半是搜索框）：退回到全量枚举再筛。
            // 注意 FindAll 给出的是 IUIAutomationElementArray（取值方法是 GetElement），
            // 不是 IUIAutomationElement —— 两者名字相近，容易写错。
            found->Release();
            found = nullptr;
            IUIAutomationElementArray* list = nullptr;
            if (SUCCEEDED(root->FindAll(TreeScope_Descendants, cond, &list)) && list) {
                int n = 0;
                list->get_Length(&n);
                for (int i = 0; i < n && result.isEmpty(); ++i) {
                    IUIAutomationElement* e = nullptr;
                    if (SUCCEEDED(list->GetElement(i, &e)) && e) {
                        if (isOmnibox(e)) result = readValue(e);
                        e->Release();
                    }
                }
                list->Release();
            }
        }
        if (found) found->Release();
    }
    cond->Release();
    root->Release();
    return result;
}

} // namespace

/**
 * 轮询线程：自带消息循环 + 自己的 COM 初始化。
 * UIA 的 COM 对象**不是线程安全的**，跨线程用会随机崩，
 * 所以整个轮询封闭在这一线程内，结果通过信号送回 GUI 线程。
 */
class EdgeWatchThread : public QThread {
public:
    EdgeWatchThread(EdgeWatch* owner, QStringList entries)
        : owner_(owner), entries_(std::move(entries)) {}

protected:
    void run() override {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            qWarning("[edgewatch] CoInitializeEx failed; watch disabled");
            return;
        }

        IUIAutomation* uia = nullptr;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&uia)))
            || !uia) {
            qWarning("[edgewatch] CUIAutomation unavailable; watch disabled");
            CoUninitialize();
            return;
        }

        QString lastUrl;      // 上轮读到的地址，用于"页面没变就不做事"
        QString alertedSite;  // 已就当前站点报过警，避免在同一网站反复弹
        int missCount = 0;    // 连续"有窗口但读不到地址"的轮数（诊断用）

        QTimer timer;
        timer.setInterval(kPollIntervalMs);
        QObject::connect(&timer, &QTimer::timeout, owner_, [this, uia, &lastUrl,
                                                             &alertedSite, &missCount, &timer]() {
            QStringList urls;
            int windows = 0;
            EnumContext ctx{uia, &urls, &windows};
            ::EnumWindows(&enumEdgeWindow, reinterpret_cast<LPARAM>(&ctx));

            // 诊断：**只有"枚举到了 Edge 窗口却读不到地址栏"才打**。
            // 这个组合意味着 UIA 侧出了意外（Edge 版本改了控件结构、更新中途、
            // provider 短暂不可用），是最需要被看见的异常。
            // "没有 Edge 窗口"是正常状态（用户没开浏览器），刷屏没意义。
            if (windows > 0 && urls.isEmpty()) {
                ++missCount;
                // 偶发一两次不算异常（页面切换瞬间会这样），连续 3 次才报
                if (missCount == 3) {
                    qWarning().noquote()
                        << QStringLiteral(
                               "[edgewatch] 枚举到 %1 个 Edge 窗口但读不到地址栏（连续 %2 次）。"
                               "可能是 Edge 更新改了地址栏控件结构，或页面正在切换")
                               .arg(windows)
                               .arg(missCount);
                }
            } else {
                missCount = 0;
            }

            if (urls.isEmpty()) {
                // 没有可读的 Edge 窗口（全在内部页 / 正在关闭 / UAC 桌面）
                lastUrl.clear();
                alertedSite.clear();
                return;
            }

            const QString url = urls.first();
            if (url == lastUrl) return;   // 页面没变，别做事
            lastUrl = url;

            if (EdgeWatch::urlAllowed(url, entries_)) {
                alertedSite.clear();     // 回到白名单内 → 允许再次报警
                return;
            }

            const QString site = EdgeWatch::displaySite(url);
            if (site == alertedSite) return;   // 同一站点不重复弹
            alertedSite = site;

            qWarning().noquote()
                << QStringLiteral("[edgewatch] 越界：%1 (%2)，白名单 %3 条")
                       .arg(site, url.left(120))
                       .arg(entries_.size());
            emit owner_->violationDetected(site, entries_);
        });

        timer.start();
        exec();   // 退出靠外层 quit()

        uia->Release();
        CoUninitialize();
    }

private:
    EdgeWatch* owner_;
    QStringList entries_;   // 快照：轮询期间用户改了白名单也不影响本轮判定
};

#else

/** 非 Windows：没有 Edge，监视器是空壳（不报警） */
class EdgeWatchThread : public QThread {
public:
    EdgeWatchThread(EdgeWatch*, QStringList) {}
};

#endif

// ---------------------------------------------------------------------------
// EdgeWatch
// ---------------------------------------------------------------------------

EdgeWatch::EdgeWatch(QObject* parent) : QObject(parent) {}

EdgeWatch::~EdgeWatch() { stop(); }

void EdgeWatch::start(const QStringList& entries) {
    stop();
    if (entries.isEmpty()) return;   // 空名单不监视：没有可判定的基准
    thread_ = new EdgeWatchThread(this, entries);
    thread_->start();
}

void EdgeWatch::stop() {
    if (!thread_) return;
    // 必须 wait：UIA 对象在该线程里 Release，线程没结束就析构会用到已释放的 COM
    thread_->quit();
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
}

} // namespace pic2pet
