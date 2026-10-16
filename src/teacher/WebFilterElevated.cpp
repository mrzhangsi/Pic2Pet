#include "WebFilterElevated.h"

#include "WebFilter.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <windows.h>
#include <shellapi.h>

#include <atomic>

namespace pic2pet {
namespace {

/** 同一次只允许一个提权任务在飞（防重复点击开出一堆 UAC） */
std::atomic_bool g_busy{false};

/** 子进程等结果的超时。写点注册表 normally 是毫秒级，30s 足够覆盖慢盘与杀软扫描 */
constexpr int kResultTimeoutMs = 30000;
constexpr int kPollIntervalMs = 50;

struct JobPaths {
    QString job;      // 父 → 子：apply 的白名单条目（clear 时是空文件）
    QString result;   // 子 → 父："ok" 或 "err: 原因"
};

JobPaths makeJobPaths() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString base = QStringLiteral("pic2pet_wf_%1_%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QUuid::createUuid().toString(QUuid::Id128));
    return JobPaths{dir + QLatin1Char('/') + base + QStringLiteral(".job"),
                    dir + QLatin1Char('/') + base + QStringLiteral(".result")};
}

bool writeEntries(const QString& path, const QStringList& entries) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    // 条目已在 apply() 里校验过不含空格与换行，所以一行一条是安全的
    for (const QString& e : entries) out << e << '\n';
    out.flush();
    return f.error() == QFile::NoError;
}

QStringList readEntries(const QString& path) {
    QStringList out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty()) out.append(line);
    }
    return out;
}

bool writeResult(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    out << text;
    out.flush();
    return f.error() == QFile::NoError;
}

/** 给命令行参数加引号：路径里有空格时不能裸拼 */
QString quoteArg(const QString& s) {
    bool needsQuote = s.isEmpty();
    for (int i = 0; i < s.size() && !needsQuote; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('"')) {
            needsQuote = true;
        }
    }
    if (!needsQuote) return s;
    QString escaped = s;
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

/**
 * 用 "runas" 拉起一个提权的自己。
 *
 * 注意：ShellExecuteEx 在用户回答 UAC 之前**不会返回**，所以调用期间本线程是阻塞的。
 * 这没法绕开（UAC 本来就是模态的安全桌面），实践上也是各家应用的做法 ——
 * 用户看到的是系统安全桌面，程序窗口本来就不该在那时候可交互。
 */
bool launchElevated(const QString& arg, const QString& jobPath, QString* error) {
    const QString exe = QCoreApplication::applicationFilePath();

    QString params = quoteArg(exe);
    params += QLatin1Char(' ');
    params += arg;
    params += QLatin1Char(' ');
    params += quoteArg(jobPath);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";                       // ← 触发 UAC
    sei.lpFile = reinterpret_cast<LPCWSTR>(exe.utf16());
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.lpDirectory = reinterpret_cast<LPCWSTR>(
        QFileInfo(exe).absolutePath().utf16());  // 工作目录 = exe 所在目录
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            *error = QStringLiteral("已取消管理员授权，未修改 Edge 策略");
        } else if (err == ERROR_ELEVATION_REQUIRED) {
            *error = QStringLiteral("当前账户不是管理员，无法修改 Edge 策略");
        } else {
            *error = QStringLiteral("提权启动失败（错误码 %1）").arg(err);
        }
        return false;
    }
    // 子进程句柄用不上（结果走文件回传），但必须关掉，否则每次开关都漏一个句柄
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

enum class PollResult { Pending, Ok, Failed };

PollResult pollOnce(const JobPaths& paths, QString* message) {
    QFile f(paths.result);
    if (!f.exists()) return PollResult::Pending;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return PollResult::Pending;

    const QString text = QString::fromUtf8(f.readAll()).trimmed();
    if (text.isEmpty()) return PollResult::Pending;   // 还没写完，等下一轮

    if (text == QLatin1String("ok")) {
        *message = QString();
        return PollResult::Ok;
    }
    // 子进程写的是 "err: 原因"
    *message = text.startsWith(QLatin1String("err:"))
                   ? text.mid(4).trimmed()
                   : QStringLiteral("提权进程返回了无法识别的结果：%1").arg(text);
    return PollResult::Failed;
}

void cleanup(const JobPaths& paths) {
    QFile::remove(paths.job);
    QFile::remove(paths.result);
}

/** 供异步版本复用的收尾：清临时文件 + 释放 busy 标志 */
void finish(const JobPaths& paths, bool ok, const QString& message,
            const std::function<void(bool, const QString&)>& done) {
    cleanup(paths);
    g_busy.store(false);
    if (done) done(ok, message);
}

/** 同步版：退出 Pic2Pet 时用，必须保证策略清干净再让进程结束 */
bool waitBlocking(const JobPaths& paths, QString* error) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kResultTimeoutMs) {
        QString message;
        switch (pollOnce(paths, &message)) {
        case PollResult::Ok:
            cleanup(paths);
            return true;
        case PollResult::Failed:
            cleanup(paths);
            *error = message;
            return false;
        case PollResult::Pending:
            break;
        }
        QThread::msleep(kPollIntervalMs);
    }
    cleanup(paths);
    *error = QStringLiteral("等待管理员进程超时（%1 秒）").arg(kResultTimeoutMs / 1000);
    return false;
}

} // namespace

bool WebFilterElevated::runChildJob(const QString& jobPath, bool isApply) {
    const JobPaths paths{jobPath, jobPath + QStringLiteral(".result")};

    QString message;
    bool ok = false;
    if (isApply) {
        WebFilter filter;
        ok = filter.apply(readEntries(jobPath));
        message = filter.error();
    } else {
        WebFilter filter;
        filter.clear();
        // clear() 不返回状态，用"策略是否还挂着"来验证真的清掉了
        ok = !WebFilter::isActive();
        message = ok ? QString()
                     : QStringLiteral("策略值未能清除（可能被安全软件拦截）");
    }

    writeResult(paths.result,
                ok ? QStringLiteral("ok") : QStringLiteral("err: ") + message);
    // 结果写不回（磁盘满/被杀）时父进程会超时，那也是明确的失败路径，不必再补救
    return true;
}

bool WebFilterElevated::applyBlocking(const QStringList& entries, QString* error) {
    error->clear();
    if (!WebFilter::isSupported()) {
        *error = QStringLiteral("网站白名单只支持 Windows");
        return false;
    }
    // 已经是提权进程（比如被当成管理员启动）就直接写，不必再弹一次 UAC
    if (WebFilter::isElevated()) {
        WebFilter filter;
        const bool ok = filter.apply(entries);
        if (!ok) *error = filter.error();
        return ok;
    }
    if (g_busy.exchange(true)) {
        *error = QStringLiteral("上一次操作还没完成，请稍候");
        return false;
    }

    const JobPaths paths = makeJobPaths();
    if (!writeEntries(paths.job, entries)) {
        g_busy.store(false);
        *error = QStringLiteral("无法写入临时文件：%1").arg(paths.job);
        return false;
    }
    if (!launchElevated(QString::fromLatin1(kApplyArg), paths.job, error)) {
        cleanup(paths);
        g_busy.store(false);
        return false;
    }
    const bool ok = waitBlocking(paths, error);
    g_busy.store(false);
    return ok;
}

bool WebFilterElevated::clearBlocking(QString* error) {
    error->clear();
    if (!WebFilter::isSupported()) return true;
    if (WebFilter::isElevated()) {
        WebFilter filter;
        filter.clear();
        return true;
    }
    if (g_busy.exchange(true)) {
        *error = QStringLiteral("上一次操作还没完成，请稍候");
        return false;
    }

    const JobPaths paths = makeJobPaths();
    if (!writeEntries(paths.job, QStringList())) {
        g_busy.store(false);
        *error = QStringLiteral("无法写入临时文件：%1").arg(paths.job);
        return false;
    }
    if (!launchElevated(QString::fromLatin1(kClearArg), paths.job, error)) {
        cleanup(paths);
        g_busy.store(false);
        return false;
    }
    const bool ok = waitBlocking(paths, error);
    g_busy.store(false);
    return ok;
}

void WebFilterElevated::applyAsync(const QStringList& entries,
                                   std::function<void(bool, const QString&)> done) {
    // 提权部分（ShellExecuteEx 等 UAC）必须在**有消息循环的线程**里做吗？不需要 ——
    // 但 Qt 的文件类不是线程安全的竞争点，所以整个流程放到独立线程，
    // 绝不在 GUI 线程上阻塞（否则设置窗口会卡住十几秒）。
    runAsync(entries, true, std::move(done));
}

void WebFilterElevated::clearAsync(std::function<void(bool, const QString&)> done) {
    runAsync(QStringList(), false, std::move(done));
}

void WebFilterElevated::runAsync(const QStringList& entries, bool isApply,
                                 std::function<void(bool, const QString&)> done) {
    QPointer<QObject> ctx(QCoreApplication::instance());
    auto* worker = QThread::create([entries, isApply, done, ctx]() {
        bool ok = false;
        QString error;
        if (isApply) {
            ok = applyBlocking(entries, &error);
        } else {
            ok = clearBlocking(&error);
        }
        // 回到 GUI 线程再回调：done 里会碰 UI / 改设置。
        // 必须先 isNull() 再 .data()：等待期间 app 可能已被销毁（退出流程），
        // 那时 invokeMethod 会拿到悬垂指针。
        if (ctx.isNull()) return;
        QMetaObject::invokeMethod(
            ctx.data(), [done, ok, error]() { if (done) done(ok, error); },
            Qt::QueuedConnection);
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

} // namespace pic2pet
