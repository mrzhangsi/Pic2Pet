#pragma once

#include <QString>

namespace pic2pet {

/**
 * 音频播放。
 *
 * 刻意**不用 Qt Multimedia**：Qt 6 在 Windows 默认走 FFmpeg 后端，会给发布包带进
 * ~90MB 的 av*.dll。这里直接用系统的 MCI（winmm），零额外体积，
 * WAV / MP3 等交给系统编解码器。
 *
 * MCI 的 alias 是进程级的，所以整个进程共用一个播放器（单例）。
 * 别处平台（macOS/Linux）走空实现，保证能编译。
 */
class AudioPlayer {
public:
    virtual ~AudioPlayer() = default;

    virtual QString name() const = 0;

    /** 播一次（会先打断上一次）。path 为空直接返回 true。 */
    virtual bool play(const QString& path) = 0;

    /** 停止并释放当前播放 */
    virtual void stop() = 0;
};

/** 进程级单例 */
AudioPlayer& audioPlayer();

} // namespace pic2pet
