#pragma once

#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * 素材加载入口：按文件**魔数**（而不是扩展名）嗅探格式并分发，
 * 扩展名只作为目录序列的过滤条件。
 */
class MediaLoader {
public:
    struct Options {
        double sequenceDelayMs = 100.0;   // 目录序列每帧时长
        bool downscaleIfHuge = true;      // 超过帧内存硬上限时自动降采样
    };

    /** 文件或目录都走这里 */
    static MediaResult loadPath(const QString& path, const Options& opt = Options());

    /** 单个文件（动图 / 静态图） */
    static MediaResult loadFile(const QString& path, const Options& opt = Options());

    /** 目录：里面的图片按文件名自然排序组成序列 */
    static MediaResult loadDirectory(const QString& dir, double delayMs = 100.0);

    static QStringList imageFilter();

private:
    static MediaResult loadStillImage(const QString& path);
    static bool applyMemoryGuard(FrameSequence& seq, QString* note);
};

} // namespace pic2pet
