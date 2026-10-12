#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>

#include "TeacherConfig.h"

class QScreen;

namespace pic2pet {

/**
 * 屏幕截取器。
 *
 * Windows 走 **Win32 GDI**（BitBlt + GetDIBits）而不是 QScreen::grabWindow()：
 * 监控跑在后台线程，QScreen::grabWindow 从非 GUI 线程调用会崩（已知问题），
 * GDI 则是线程安全的。其他平台（只求能编译）退回 queued invoke 到主线程。
 *
 * 抓取区域在启动时由主线程算好一次（primaryScreen 的 geometry），
 * 之后工作线程只按这个矩形 BitBlt，不再碰任何 Qt GUI 对象。
 */
class ScreenGrabber {
public:
    ScreenGrabber();
    ~ScreenGrabber();

    /** 抓一帧（BGRA → QImage::Format_ARGB32）。失败返回空 QImage */
    QImage grab();

    bool valid() const { return !rect_.isEmpty(); }

private:
    QRect rect_;   // 虚拟桌面坐标系里的目标矩形（启动时在主线程填好）

#ifdef Q_OS_WIN
    void* dcScreen_ = nullptr;   // HDC（整个虚拟桌面），复用避免每次 GetDC
    void* dcMem_ = nullptr;      // HDC 兼容内存 DC
    void* bitmap_ = nullptr;     // HBITMAP
    void* prevBitmap_ = nullptr; // 换回旧位图用（析构时恢复）
    int bmpW_ = 0;
    int bmpH_ = 0;
#endif
};

/** 把屏幕（主）与摄像头（辅）**左右**拼成一张 JPEG，只发一次请求、只算一份图片 token */
QByteArray teacherComposeJpeg(const QImage& screen, const QImage* camera,
                              const TeacherCaptureCfg& cfg);

/** 帧差检测用的小灰度图（Format_Grayscale8） */
QImage teacherToThumb(const QImage& img, int width);

/** 转成 API 需要的 data URL */
QString teacherToDataUrl(const QByteArray& jpeg);

/** 两张灰度缩略图的平均绝对差（0~255） */
double teacherGrayDiff(const QImage& a, const QImage& b);

} // namespace pic2pet
