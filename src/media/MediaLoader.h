#pragma once

#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * 素材加载入口：按文件**魔数**（而不是扩展名）嗅探格式并分发。
 *
 * 只处理单个素材文件；**不支持"把一个目录当逐帧序列"**。
 */
class MediaLoader {
public:
    struct Options {
        bool downscaleIfHuge = true;      // 超过帧内存硬上限时自动降采样
    };

    /** 单个素材文件。传目录会直接报错。 */
    static MediaResult loadPath(const QString& path, const Options& opt = Options());

    /** 单个文件（动图 / 静态图） */
    static MediaResult loadFile(const QString& path, const Options& opt = Options());

private:
    static MediaResult loadStillImage(const QString& path);
    static bool applyMemoryGuard(FrameSequence& seq, QString* note);
};

} // namespace pic2pet
