#pragma once

#include <QtGlobal>

#include <QByteArray>
#include <QString>
#include <QVector>

namespace pic2pet {

/**
 * M1 的统一素材产物：所有格式最终都变成一串 RGBA8 全画布帧。
 *
 * 设计要点（见技术方案 4.2 / 12.1）：
 *   - 加载时**一次性全解码**为 RGBA8 常驻内存，播放期不做任何解码（照搬 desktop-pet2）
 *   - 每帧尺寸统一为画布尺寸，避免渲染时因帧尺寸不同而反复重建纹理
 *   - `starts` 是每帧起始时间的缓存，让 frameIndexAt() 不必每次累加求和
 */
struct FrameSequence {
    int width = 0;
    int height = 0;

    QVector<QByteArray> frames;   // 每帧 width*height*4，straight alpha，行优先
    QVector<double> delays;       // 每帧持续时长（秒）
    QVector<double> starts;       // 每帧起始时刻（秒），长度同 delays
    double totalDuration = 0.0;   // 静态图时为 0
    int loopCount = 0;            // 0 = 无限循环（NETSCAPE2.0 / acTL 语义）
    QString sourceName;           // 仅用于日志

    bool isValid() const { return !frames.isEmpty() && width > 0 && height > 0; }
    bool isStatic() const { return frames.size() <= 1; }
    int count() const { return frames.size(); }

    /** 帧数据总字节数，用于内存预算检查 */
    qint64 bytes() const;

    double delayAt(int i) const;

    /** 给定播放时刻（秒，已取模到 [0, totalDuration)），返回应显示的帧下标 */
    int frameIndexAt(double t) const;

    void rebuildStarts();
};

/** 帧内存硬上限，超过则提示并降采样（技术方案 12.1） */
constexpr qint64 kMaxFrameBytes = 150LL * 1024 * 1024;

/** 所有加载器统一的返回结构 */
struct MediaResult {
    bool ok = false;
    QString error;          // ok == false 时的原因
    FrameSequence seq;      // ok == true 时的产物
};

} // namespace pic2pet
