#include "MediaLoader.h"

#include "ApngDecoder.h"
#include "GifDecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>

#include <cmath>

namespace pic2pet {

namespace {

QByteArray toRgba8888(const QImage& img) {
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) return QByteArray();
    // QByteArray(ptr, size) 会做深拷贝，Pointer 指向 QImage 内部数据也没问题
    return QByteArray(reinterpret_cast<const char*>(rgba.constBits()),
                      int(rgba.sizeInBytes()));
}

/** 双线性重采样，用于触发内存上限后的降采样 */
QByteArray resampleRgba(const uchar* src, int sw, int sh, int dw, int dh) {
    QByteArray out(dw * dh * 4, '\0');
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return out;
    uchar* dst = reinterpret_cast<uchar*>(out.data());

    const double fx = double(sw) / double(dw);
    const double fy = double(sh) / double(dh);

    for (int y = 0; y < dh; ++y) {
        const double sy = std::min<double>(sh - 1, std::max<double>(0.0, y * fy + fy * 0.5 - 0.5));
        const int y0 = int(std::floor(sy));
        const int y1 = std::min(sh - 1, y0 + 1);
        const double wy = sy - y0;

        for (int x = 0; x < dw; ++x) {
            const double sx = std::min<double>(sw - 1, std::max<double>(0.0, x * fx + fx * 0.5 - 0.5));
            const int x0 = int(std::floor(sx));
            const int x1 = std::min(sw - 1, x0 + 1);
            const double wx = sx - x0;

            const uchar* p00 = src + (qint64(y0) * sw + x0) * 4;
            const uchar* p10 = src + (qint64(y0) * sw + x1) * 4;
            const uchar* p01 = src + (qint64(y1) * sw + x0) * 4;
            const uchar* p11 = src + (qint64(y1) * sw + x1) * 4;

            uchar* d = dst + (qint64(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] * (1.0 - wx) + p10[c] * wx;
                const double bottom = p01[c] * (1.0 - wx) + p11[c] * wx;
                d[c] = uchar(top * (1.0 - wy) + bottom * wy);
            }
        }
    }
    return out;
}

} // namespace

MediaResult MediaLoader::loadPath(const QString& path, const Options& opt) {
    const QFileInfo info(path);
    if (!info.exists()) {
        MediaResult r;
        r.error = QStringLiteral("路径不存在：%1").arg(path);
        return r;
    }
    if (info.isDir()) {
        // 只支持单个素材文件，不再支持"把一个目录当逐帧序列"
        MediaResult r;
        r.error = QStringLiteral("请选择素材文件，不支持文件夹：%1").arg(path);
        return r;
    }
    return loadFile(path, opt);
}

MediaResult MediaLoader::loadFile(const QString& path, const Options& opt) {
    MediaResult r;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("打不开文件：%1").arg(path);
        return r;
    }
    // 只需要读头部就能判断格式，避免把大文件整个读进内存两次
    const QByteArray head = f.peek(64);
    f.close();

    if (GifDecoder::sniff(head)) {
        MediaResult g = GifDecoder::decodeFile(path);
        if (g.ok) {
            QString note;
            if (opt.downscaleIfHuge) applyMemoryGuard(g.seq, &note);
            g.seq.sourceName = QFileInfo(path).fileName();
        }
        return g;
    }

    if (ApngDecoder::sniffPng(head)) {
        MediaResult a = ApngDecoder::decodeFile(path);
        if (a.ok) {
            QString note;
            if (opt.downscaleIfHuge) applyMemoryGuard(a.seq, &note);
            a.seq.sourceName = QFileInfo(path).fileName();
            return a;
        }
        // 普通 PNG 走"静态图"是**正常路径**，不要报警（否则一张 PNG 就刷一条日志）。
        // 只有"确实是 APNG 却没解开"才值得报——静默降级会让这种情况看起来像素材本来就是静态图。
        if (a.error != ApngDecoder::notAnimatedError()) {
            qWarning().noquote() << "[media] APNG 解码未成功，按静态图处理：" << path
                                 << "（" << a.error << "）";
        }
    }

    return loadStillImage(path);
}

MediaResult MediaLoader::loadStillImage(const QString& path) {
    MediaResult r;

    QImage img(path);
    if (img.isNull()) {
        r.error = QStringLiteral("无法解码图片：%1").arg(path);
        return r;
    }

    const QByteArray bits = toRgba8888(img);
    if (bits.isEmpty()) {
        r.error = QStringLiteral("格式转换失败：%1").arg(path);
        return r;
    }

    FrameSequence seq;
    seq.width = img.width();
    seq.height = img.height();
    seq.frames.push_back(bits);
    seq.delays.push_back(0.0);        // 静态图：totalDuration = 0，播放定时器会直接停
    seq.rebuildStarts();
    seq.sourceName = QFileInfo(path).fileName();

    QString note;
    applyMemoryGuard(seq, &note);

    r.ok = true;
    r.seq = seq;
    return r;
}

bool MediaLoader::applyMemoryGuard(FrameSequence& seq, QString* note) {
    if (!seq.isValid()) return false;
    if (seq.bytes() <= kMaxFrameBytes) return false;

    const double ratio = std::sqrt(double(kMaxFrameBytes) / double(seq.bytes()));
    const int dw = std::max(1, int(seq.width * ratio));
    const int dh = std::max(1, int(seq.height * ratio));

    for (int i = 0; i < seq.frames.size(); ++i) {
        const QByteArray& src = seq.frames.at(i);
        seq.frames[i] = resampleRgba(reinterpret_cast<const uchar*>(src.constData()),
                                     seq.width, seq.height, dw, dh);
    }
    seq.width = dw;
    seq.height = dh;

    if (note) {
        *note = QStringLiteral("帧内存超过 %1 MB，已自动降采样到 %2x%3")
                    .arg(kMaxFrameBytes / (1024 * 1024))
                    .arg(dw)
                    .arg(dh);
    }
    return true;
}

} // namespace pic2pet
