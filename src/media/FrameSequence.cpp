#include "FrameSequence.h"

namespace pic2pet {

qint64 FrameSequence::bytes() const {
    qint64 total = 0;
    for (const QByteArray& f : frames) total += f.size();
    return total;
}

double FrameSequence::delayAt(int i) const {
    if (i < 0 || i >= delays.size()) return 0.0;
    return delays.at(i);
}

void FrameSequence::rebuildStarts() {
    starts.clear();
    starts.reserve(delays.size());
    double acc = 0.0;
    for (double d : delays) {
        starts.push_back(acc);
        acc += d;
    }
    totalDuration = acc;
}

int FrameSequence::frameIndexAt(double t) const {
    if (frames.isEmpty()) return 0;
    if (delays.size() != frames.size()) return 0;

    const int n = frames.size();
    if (n == 1 || totalDuration <= 0.0) return 0;

    // 调用方通常已取模，这里再兜一次底，防止浮点误差越界
    double x = t;
    if (x < 0.0) x = 0.0;
    while (x >= totalDuration) x -= totalDuration;

    // 帧数最多几百，线性扫描比维护二分结构更省心，也足够快
    for (int i = n - 1; i >= 0; --i) {
        if (x >= starts.at(i)) return i;
    }
    return 0;
}

} // namespace pic2pet
