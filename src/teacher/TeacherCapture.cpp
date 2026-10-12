#include "TeacherCapture.h"

#include <QBuffer>
#include <QImageWriter>
#include <QPainter>

#ifdef Q_OS_WIN
// windows.h 里的 min/max 宏会破坏 std::min / std::max，必须先禁掉
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <QGuiApplication>
#  include <QScreen>
#endif

namespace pic2pet {

ScreenGrabber::ScreenGrabber() {
#ifdef Q_OS_WIN
    // 只抓**主显示器**（对齐参考实现的 mss.monitors[1]）。
    // BitBlt 的源坐标是虚拟桌面坐标，主屏左上角未必是 (0,0)。
    const POINT origin = {0, 0};
    const HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(mon, &mi)) {
        rect_ = QRect(mi.rcMonitor.left, mi.rcMonitor.top,
                      mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top);
    }

    dcScreen_ = GetDC(nullptr);   // 整个虚拟桌面
    if (dcScreen_ && !rect_.isEmpty()) {
        dcMem_ = CreateCompatibleDC(static_cast<HDC>(dcScreen_));
        if (dcMem_) {
            bitmap_ = CreateCompatibleBitmap(static_cast<HDC>(dcScreen_),
                                             rect_.width(), rect_.height());
            if (bitmap_) {
                prevBitmap_ = SelectObject(static_cast<HDC>(dcMem_), bitmap_);
                bmpW_ = rect_.width();
                bmpH_ = rect_.height();
            }
        }
    }
#else
    // 只求能编译：抓整块主屏，grab() 时会 queued 回到 GUI 线程执行
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) rect_ = screen->geometry();
#endif
}

ScreenGrabber::~ScreenGrabber() {
#ifdef Q_OS_WIN
    if (dcMem_) {
        if (prevBitmap_) SelectObject(static_cast<HDC>(dcMem_), prevBitmap_);
        DeleteDC(static_cast<HDC>(dcMem_));
    }
    if (bitmap_) DeleteObject(bitmap_);
    if (dcScreen_) ReleaseDC(nullptr, static_cast<HDC>(dcScreen_));
#endif
}

QImage ScreenGrabber::grab() {
#ifdef Q_OS_WIN
    if (!dcScreen_ || !dcMem_ || !bitmap_ || rect_.isEmpty()) return QImage();

    const int w = rect_.width();
    const int h = rect_.height();
    if (w != bmpW_ || h != bmpH_) return QImage();

    if (!BitBlt(static_cast<HDC>(dcMem_), 0, 0, w, h,
                static_cast<HDC>(dcScreen_), rect_.x(), rect_.y(), SRCCOPY)) {
        return QImage();
    }

    QImage img(w, h, QImage::Format_ARGB32);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 负数 = 自顶向下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    // 32bpp BI_RGB DIB 的内存布局（LE）是 B,G,R,A，与 QImage::Format_ARGB32 一致
    if (!GetDIBits(static_cast<HDC>(dcMem_), static_cast<HBITMAP>(bitmap_), 0, h,
                   img.bits(), &bi, DIB_RGB_COLORS)) {
        return QImage();
    }
    return img;
#else
    // 只求能编译：QScreen::grabWindow 必须回 GUI 线程调
    return QMetaObject::invokeMethod(qApp, [] {
        const QScreen* screen = QGuiApplication::primaryScreen();
        return screen ? screen->grabWindow(0).toImage() : QImage();
    }, Qt::BlockingQueuedConnection);
#endif
}

QByteArray teacherComposeJpeg(const QImage& screen, const QImage* camera,
                              const TeacherCaptureCfg& cfg) {
    // 统一转成不透明的 RGB32：桌面上抓来的图 alpha 通道是垃圾，
    // 直接拿 ARGB32 去 drawImage 会按 alpha 混合、把画面混没。
    const int totalW = qMax(64, cfg.composeWidth);
    const int camW = camera ? qMax(0, int(totalW * cfg.cameraRatio)) : 0;
    const int screenW = qMax(1, totalW - camW);

    QImage sp = screen.isNull() ? QImage() : screen.convertToFormat(QImage::Format_RGB32);
    if (!sp.isNull()) {
        sp = sp.scaledToWidth(screenW, Qt::SmoothTransformation);
    }

    QImage merged;
    if (camW <= 0 || !camera) {
        merged = sp;
    } else {
        QImage cp = camera->convertToFormat(QImage::Format_RGB32)
                        .scaledToWidth(camW, Qt::SmoothTransformation);
        const int h = qMax(sp.isNull() ? 0 : sp.height(), cp.height());
        if (h <= 0) return QByteArray();
        merged = QImage(totalW, h, QImage::Format_RGB32);
        merged.fill(Qt::black);
        QPainter p(&merged);
        if (!sp.isNull()) p.drawImage(0, 0, sp);
        p.drawImage(screenW, 0, cp);
        p.end();
    }

    if (merged.isNull()) return QByteArray();

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "JPG");
    writer.setQuality(cfg.jpegQuality);
    if (!writer.write(merged)) return QByteArray();
    return out;
}

QImage teacherToThumb(const QImage& img, int width) {
    if (img.isNull()) return QImage();
    QImage s = (width > 1 && img.width() > width)
                   ? img.scaledToWidth(width, Qt::SmoothTransformation)
                   : img.copy();
    return s.convertToFormat(QImage::Format_Grayscale8);
}

QString teacherToDataUrl(const QByteArray& jpeg) {
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
}

double teacherGrayDiff(const QImage& a, const QImage& b) {
    if (a.isNull() || b.isNull()) return -1.0;
    if (a.size() != b.size() || a.format() != QImage::Format_Grayscale8
        || b.format() != QImage::Format_Grayscale8) {
        return -1.0;   // 没有可比基准
    }
    const int w = a.width();
    const int h = a.height();
    qint64 sum = 0;
    for (int y = 0; y < h; ++y) {
        const uchar* pa = a.scanLine(y);
        const uchar* pb = b.scanLine(y);
        for (int x = 0; x < w; ++x) sum += qAbs(int(pa[x]) - int(pb[x]));
    }
    return double(sum) / double(qMax(1, w * h));
}

} // namespace pic2pet
