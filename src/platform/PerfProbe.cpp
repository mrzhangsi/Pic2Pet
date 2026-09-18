#include "PerfProbe.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QList>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#else
#  include <sys/resource.h>
#  include <unistd.h>
#endif

#ifdef Q_OS_MACOS
#  include <mach/mach.h>
#  include <mach/task_info.h>
#endif

namespace pic2pet {

namespace {

qint64 wallMs() {
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    return timer.elapsed();
}

quint64 cpuTicks100ns() {
#ifdef Q_OS_WIN
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart);   // 单位：100ns
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    const quint64 usec =
        quint64(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000ULL +
        quint64(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
    return usec * 10ULL;                // 微秒 → 100ns
#endif
}

quint64 residentBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return 0;
    return counters.WorkingSetSize;
#elif defined(Q_OS_MACOS)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return info.resident_size;
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/statm"));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray line = file.readLine();
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 2) return 0;
    const quint64 pages = parts[1].toULongLong();
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    return pageSize > 0 ? pages * quint64(pageSize) : 0;
#else
    return 0;
#endif
}

} // namespace

PerfProbe::PerfProbe() = default;

PerfSample PerfProbe::sample() {
    const quint64 cpu = cpuTicks100ns();
    const qint64 now = wallMs();

    PerfSample out;
    out.memoryBytes = residentBytes();

    if (!primed_) {
        lastCpu100ns_ = cpu;
        lastWallMs_ = now;
        primed_ = true;
        return out;                 // 首次调用只建立基线
    }

    const qint64 wallDelta = now - lastWallMs_;
    if (wallDelta > 0 && cpu >= lastCpu100ns_) {
        const double cpuMs = double(cpu - lastCpu100ns_) / 10000.0;   // 100ns → ms
        out.cpuPercent = cpuMs / double(wallDelta) * 100.0;
    }

    lastCpu100ns_ = cpu;
    lastWallMs_ = now;
    return out;
}

QString PerfProbe::formatBytes(quint64 bytes) {
    if (bytes == 0) return QStringLiteral("n/a");
    const double mb = double(bytes) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

} // namespace pic2pet
