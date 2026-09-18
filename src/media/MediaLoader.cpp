#include "MediaLoader.h"

#include "ApngDecoder.h"
#include "GifDecoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>

#include <algorithm>

#include <cmath>

namespace pic2pet {

namespace {

/** 自然排序：名字里的数字按数值比（frame2 < frame10） */
bool naturalLess(const QString& a, const QString& b) {
    int ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        const ushort ca = a.at(ia).unicode();
        const ushort cb = b.at(ib).unicode();
        if (ca >= '0' && ca <= '9' && cb >= '0' && cb <= '9') {
            int ja = ia, jb = ib;
            while (ja < a.size() && a.at(ja) >= '0' && a.at(ja) <= '9') ++ja;
            while (jb < b.size() && b.at(jb) >= '0' && b.at(jb) <= '9') ++jb;
            const int na = a.mid(ia, ja - ia).toInt();
            const int nb = b.mid(ib, jb - ib).toInt();
            if (na != nb) return na < nb;
            if ((ja - ia) != (jb - ib)) return (ja - ia) < (jb - ib);
            ia = ja;
            ib = jb;
            continue;
        }
        if (ca != cb) return ca < cb;
        ++ia;
        ++ib;
    }
    return a.size() < b.size();
}

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

QStringList MediaLoader::imageFilter() {
    return QStringList() << QStringLiteral("*.png") << QStringLiteral("*.jpg")
                         << QStringLiteral("*.jpeg") << QStringLiteral("*.bmp")
                         << QStringLiteral("*.webp");
}

MediaResult MediaLoader::loadPath(const QString& path, const Options& opt) {
    const QFileInfo info(path);
    if (!info.exists()) {
        MediaResult r;
        r.error = QStringLiteral("路径不存在：%1").arg(path);
        return r;
    }
    if (info.isDir()) return loadDirectory(path, opt.sequenceDelayMs);
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
        // 不是动画 PNG 就退回静态图处理，而不是报错
    }

    return loadStillImage(path);
}

MediaResult MediaLoader::loadDirectory(const QString& dir, double delayMs) {
    MediaResult r;

    QDir d(dir);
    const QStringList names = d.entryList(imageFilter(), QDir::Files, QDir::Name);
    if (names.isEmpty()) {
        r.error = QStringLiteral("目录里没有可用图片：%1").arg(dir);
        return r;
    }

    QStringList sorted = names;
    std::sort(sorted.begin(), sorted.end(), naturalLess);

    FrameSequence seq;
    const double delaySec = std::max(0.01, delayMs / 1000.0);

    for (const QString& name : sorted) {
        const QString full = d.filePath(name);
        QImage img(full);
        if (img.isNull()) continue;

        QImage normalized = img;
        if (seq.isValid()) {
            if (img.width() != seq.width || img.height() != seq.height) {
                normalized = img.scaled(seq.width, seq.height, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
            }
        } else {
            seq.width = img.width();
            seq.height = img.height();
        }

        const QByteArray bits = toRgba8888(normalized);
        if (bits.isEmpty()) continue;

        if (bits.size() < seq.width * seq.height * 4) continue;

        seq.frames.push_back(bits);
        seq.delays.push_back(delaySec);
    }

    if (seq.frames.isEmpty()) {
        r.error = QStringLiteral("目录里的图片都读取失败：%1").arg(dir);
        return r;
    }

    seq.rebuildStarts();
    seq.sourceName = QFileInfo(dir).fileName();

    QString note;
    applyMemoryGuard(seq, &note);

    r.ok = true;
    r.seq = seq;
    return r;
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
