#pragma once

#include <QImage>

namespace pic2pet {

/**
 * 摄像头读取器（Media Foundation）。
 *
 * 为什么不用 Qt Multimedia：Qt 6 在 Windows 默认走 FFmpeg 后端，会给发布包
 * 带进 ~90MB 的 av*.dll（见 platform/AudioPlayer.h 的同一决策）。
 * MF 是系统组件，零额外体积。
 *
 * 设备缺失 / 被占用时 read() 返回空 QImage，主流程自动降级为仅凭屏幕判定。
 * 只实现 Windows；其他平台返回空（保证能编译）。
 */
class CameraReader {
public:
    explicit CameraReader(int index = 0);
    ~CameraReader();

    CameraReader(const CameraReader&) = delete;
    CameraReader& operator=(const CameraReader&) = delete;

    /** 读一帧（MFVideoFormat_RGB32 → QImage::Format_ARGB32）。失败返回空 QImage */
    QImage read();

    bool valid() const { return valid_; }

private:
    void* reader_ = nullptr;   // IMFSourceReader*
    bool valid_ = false;
};

} // namespace pic2pet
