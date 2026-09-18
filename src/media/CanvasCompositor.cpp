#include "CanvasCompositor.h"

#include <QtGlobal>

#include <cstring>

namespace pic2pet {

CanvasCompositor::CanvasCompositor(int width, int height)
    : width_(width), height_(height) {
    if (width_ > 0 && height_ > 0) {
        buffer_.resize(qint64(width_) * height_ * 4, '\0');
    }
}

void CanvasCompositor::reset() {
    buffer_.fill('\0');
}

QRect CanvasCompositor::clip(const QRect& r) const {
    QRect c = r;
    if (c.left() < 0) { c.setLeft(0); }
    if (c.top() < 0) { c.setTop(0); }
    if (c.right() > width_ - 1) { c.setRight(width_ - 1); }
    if (c.bottom() > height_ - 1) { c.setBottom(height_ - 1); }
    if (c.isEmpty()) return QRect();
    return c;
}

void CanvasCompositor::blendSource(const uchar* src, int sw, int sh, int dx, int dy) {
    if (buffer_.isEmpty() || sw <= 0 || sh <= 0) return;

    const QRect target = clip(QRect(dx, dy, sw, sh));
    if (!target.isValid()) return;

    uchar* canvasBits = reinterpret_cast<uchar*>(buffer_.data());
    const int canvasStride = width_ * 4;

    for (int row = 0; row < target.height(); ++row) {
        const int sy = target.top() - dy + row;
        if (sy < 0 || sy >= sh) continue;

        const int sx = target.left() - dx;
        if (sx < 0 || sx >= sw) continue;

        uchar* dst = canvasBits + (qint64(target.top() + row) * canvasStride) + target.left() * 4;
        const uchar* line = src + (qint64(sy) * sw * 4) + sx * 4;
        uchar* d = dst;
        const uchar* s = line;

        for (int x = 0; x < target.width(); ++x, d += 4, s += 4) {
            const uchar alpha = s[3];
            if (alpha == 0) continue;              // 透明像素：保留底下内容
            if (alpha == 255) {                     // 完全不透明：整像素覆盖
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
                continue;
            }
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    }
}

void CanvasCompositor::blendOver(const uchar* src, int sw, int sh, int dx, int dy) {
    if (buffer_.isEmpty() || sw <= 0 || sh <= 0) return;

    const QRect target = clip(QRect(dx, dy, sw, sh));
    if (!target.isValid()) return;

    uchar* canvasBits = reinterpret_cast<uchar*>(buffer_.data());
    const int canvasStride = width_ * 4;

    for (int row = 0; row < target.height(); ++row) {
        const int sy = target.top() - dy + row;
        if (sy < 0 || sy >= sh) continue;
        const int sx = target.left() - dx;
        if (sx < 0 || sx >= sw) continue;

        uchar* d = canvasBits + (qint64(target.top() + row) * canvasStride) + target.left() * 4;
        const uchar* s = src + (qint64(sy) * sw * 4) + sx * 4;

        for (int x = 0; x < target.width(); ++x, d += 4, s += 4) {
            const int sa = s[3];
            if (sa == 0) continue;
            if (sa == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                continue;
            }
            const int da = d[3];
            // dst over src 的合成结果 alpha（straight alpha）
            const double sf = sa / 255.0;
            const double df = da / 255.0;
            const double oa = sf + df * (1.0 - sf);
            if (oa <= 0.0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }

            d[0] = uchar((s[0] * sf + d[0] * df * (1.0 - sf)) / oa);
            d[1] = uchar((s[1] * sf + d[1] * df * (1.0 - sf)) / oa);
            d[2] = uchar((s[2] * sf + d[2] * df * (1.0 - sf)) / oa);
            d[3] = uchar(oa * 255.0);
        }
    }
}

void CanvasCompositor::clearRect(const QRect& r) {
    const QRect target = clip(r);
    if (!target.isValid()) return;

    uchar* canvasBits = reinterpret_cast<uchar*>(buffer_.data());
    const int canvasStride = width_ * 4;
    for (int row = 0; row < target.height(); ++row) {
        uchar* d = canvasBits + (qint64(target.top() + row) * canvasStride) + target.left() * 4;
        std::memset(d, 0, size_t(target.width()) * 4);
    }
}

QByteArray CanvasCompositor::grab(const QRect& r) const {
    const QRect target = clip(r);
    if (!target.isValid()) return QByteArray();

    QByteArray out;
    out.resize(target.width() * target.height() * 4);
    const uchar* canvasBits = reinterpret_cast<const uchar*>(buffer_.constData());
    const int canvasStride = width_ * 4;
    uchar* dst = reinterpret_cast<uchar*>(out.data());

    for (int row = 0; row < target.height(); ++row) {
        const uchar* s = canvasBits + (qint64(target.top() + row) * canvasStride) + target.left() * 4;
        std::memcpy(dst + (qint64(row) * target.width() * 4), s, size_t(target.width()) * 4);
    }
    return out;
}

void CanvasCompositor::put(const QRect& r, const QByteArray& buf) {
    const QRect target = clip(r);
    if (!target.isValid()) return;
    if (buf.size() < target.width() * target.height() * 4) return;

    uchar* canvasBits = reinterpret_cast<uchar*>(buffer_.data());
    const int canvasStride = width_ * 4;
    const uchar* src = reinterpret_cast<const uchar*>(buf.constData());

    for (int row = 0; row < target.height(); ++row) {
        uchar* d = canvasBits + (qint64(target.top() + row) * canvasStride) + target.left() * 4;
        std::memcpy(d, src + (qint64(row) * target.width() * 4), size_t(target.width()) * 4);
    }
}

} // namespace pic2pet
