#pragma once

#include <QtGlobal>
#include <QString>

namespace pic2pet {

struct PerfSample {
    double cpuPercent = 0.0;      // 100% = 占满一个逻辑核心
    quint64 memoryBytes = 0;      // 工作集 / RSS
};

/**
 * 跨平台 CPU 与内存采样（M0 的验收工具）。
 * Windows 用 GetProcessTimes / GetProcessMemoryInfo，
 * POSIX 用 getrusage，Linux 读 /proc/self/statm，macOS 读 mach_task_basic_info。
 */
class PerfProbe {
public:
    PerfProbe();

    /** 返回自上次调用以来的 CPU 占比与当前常驻内存 */
    PerfSample sample();

    static QString formatBytes(quint64 bytes);

private:
    quint64 lastCpu100ns_ = 0;
    qint64 lastWallMs_ = 0;
    bool primed_ = false;
};

} // namespace pic2pet
