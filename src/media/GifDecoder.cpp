#include "GifDecoder.h"

#include "CanvasCompositor.h"

#include <QFile>
#include <QRect>

#include <cstring>

namespace pic2pet {

namespace {

constexpr int kMaxCanvasPixels = 4096 * 4096;   // 防止畸形尺寸申请超大内存

/** GIF 是小端存储 */
class ByteReader {
public:
    ByteReader(const uchar* p, qint64 n) : p_(p), n_(n) {}

    bool atEnd() const { return pos_ >= n_; }
    qint64 remaining() const { return n_ - pos_; }

    uchar byte() {
        if (pos_ >= n_) { failed_ = true; return 0; }
        return p_[pos_++];
    }

    quint16 u16() {
        const uchar lo = byte();
        const uchar hi = byte();
        return quint16(lo | (hi << 8));
    }

    bool read(uchar* dst, qint64 count) {
        if (count < 0 || remaining() < count) { failed_ = true; return false; }
        if (count > 0) std::memcpy(dst, p_ + pos_, size_t(count));
        pos_ += count;
        return true;
    }

    void skip(qint64 count) {
        if (count > remaining()) { failed_ = true; pos_ = n_; return; }
        pos_ += count;
    }

    bool failed() const { return failed_; }

private:
    const uchar* p_ = nullptr;
    qint64 n_ = 0;
    qint64 pos_ = 0;
    bool failed_ = false;
};

struct Palette {
    QByteArray rgb;     // 3 * size 字节
    int size = 0;
};

void fetchColor(const Palette& plt, int index, uchar out[3]) {
    if (plt.size <= 0) { out[0] = out[1] = out[2] = 0; return; }
    int i = index;
    if (i < 0) i = 0;
    if (i >= plt.size) i = plt.size - 1;      // 越界索引夹紧，避免读到脏内存
    const uchar* e = reinterpret_cast<const uchar*>(plt.rgb.constData()) + i * 3;
    out[0] = e[0];
    out[1] = e[1];
    out[2] = e[2];
}

/** 读取一串子块（首字节为长度，0 长度表示结束） */
QByteArray readSubBlocks(ByteReader& rd) {
    QByteArray out;
    while (!rd.atEnd()) {
        const uchar n = rd.byte();
        if (n == 0) break;
        const qint64 old = out.size();
        out.resize(int(old + n));
        if (!rd.read(reinterpret_cast<uchar*>(out.data()) + old, n)) break;
    }
    return out;
}

/**
 * GIF 变长 LZW 解码。
 * 输出 indexCount 个调色板索引到 out；解出来的顺序是「按数据传输顺序」，
 * 交织的情况由调用方重排。
 */
bool lzwDecode(const QByteArray& data, uchar minCodeSize, int indexCount, QByteArray& out) {
    if (minCodeSize < 2 || minCodeSize > 8) return false;
    if (indexCount <= 0) { out.clear(); return true; }

    const int clearCode = 1 << minCodeSize;
    const int eoiCode = clearCode + 1;

    out.resize(indexCount);
    uchar* dst = reinterpret_cast<uchar*>(out.data());
    int produced = 0;

    // 字典用「前缀码 + 后缀字节」表示，避免存字符串导致大量拷贝
    QVector<qint16> prefix(4096, -1);
    QVector<uchar> suffix(4096, 0);
    for (int i = 0; i < clearCode && i < 4096; ++i) {
        prefix[i] = -1;
        suffix[i] = uchar(i);
    }

    int codeSize = minCodeSize + 1;
    int dictSize = eoiCode + 1;
    int prev = -1;

    const uchar* bytes = reinterpret_cast<const uchar*>(data.constData());
    const int len = data.size();
    quint32 bitBuf = 0;
    int bitCnt = 0;
    int bytePos = 0;
    bool eof = false;

    auto readCode = [&](int bits) -> int {
        while (bitCnt < bits) {
            if (bytePos >= len) { eof = true; return -1; }
            bitBuf |= quint32(bytes[bytePos++]) << bitCnt;
            bitCnt += 8;
        }
        const quint32 mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        const int value = int(bitBuf & mask);
        bitBuf >>= bits;
        bitCnt -= bits;
        return value;
    };

    auto firstByteOf = [&](int code) -> uchar {
        int c = code;
        int guard = 0;
        while (prefix[c] >= 0 && guard++ < 4096) c = prefix[c];
        return suffix[c];
    };

    auto emitCode = [&](int code) -> bool {
        uchar stack[4096];
        int n = 0;
        int c = code;
        int guard = 0;
        while (c >= 0) {
            if (n >= 4096 || guard++ > 4096) return false;
            stack[n++] = suffix[c];
            c = prefix[c];
        }
        for (int i = n - 1; i >= 0; --i) {
            if (produced >= indexCount) return true;     // 多余的像素丢弃，不算错误
            dst[produced++] = stack[i];
        }
        return true;
    };

    // 迭代上限：防止畸形流导致死循环
    const qint64 maxIterations = qint64(indexCount) * 2 + 65536;
    qint64 iterations = 0;

    while (produced < indexCount) {
        if (eof) break;
        if (++iterations > maxIterations) break;

        const int code = readCode(codeSize);
        if (code < 0) break;

        if (code == clearCode) {
            codeSize = minCodeSize + 1;
            dictSize = eoiCode + 1;
            prev = -1;
            continue;
        }
        if (code == eoiCode) break;

        if (prev < 0) {
            if (code >= clearCode) break;                // 非法：首码必须是根码
            if (!emitCode(code)) return false;
            prev = code;
            continue;
        }

        if (code == dictSize) {
            // KWKwK 情形：新条目 = prev + prev 的首字节
            if (dictSize < 4096) {
                prefix[dictSize] = qint16(prev);
                suffix[dictSize] = firstByteOf(prev);
                ++dictSize;
                if (dictSize == (1 << codeSize) && codeSize < 12) ++codeSize;
            }
            if (!emitCode(code)) return false;
            prev = code;
        } else if (code < dictSize) {
            if (dictSize < 4096) {
                prefix[dictSize] = qint16(prev);
                suffix[dictSize] = firstByteOf(code);
                ++dictSize;
                if (dictSize == (1 << codeSize) && codeSize < 12) ++codeSize;
            }
            if (!emitCode(code)) return false;
            prev = code;
        } else {
            break;                                        // 超出字典范围的非法码
        }
    }

    if (produced <= 0) return false;
    if (produced < indexCount) {
        // 少数素材的数据块被截断：剩余像素填 0，而不是让整个素材加载失败
        std::memset(dst + produced, 0, size_t(indexCount - produced));
    }
    return true;
}

/** interlace 的 4-pass 行序还原 */
void deinterlace(const QByteArray& in, QByteArray& out, int width, int height) {
    out.resize(width * height);
    out.fill('\0');
    const uchar* src = reinterpret_cast<const uchar*>(in.constData());
    uchar* dst = reinterpret_cast<uchar*>(out.data());

    if (width <= 0) return;

    static const int starts[4] = { 0, 4, 2, 1 };
    static const int steps[4]  = { 8, 8, 4, 2 };

    int consumed = 0;
    const qint64 total = qint64(width) * height;
    for (int pass = 0; pass < 4; ++pass) {
        for (int y = starts[pass]; y < height; y += steps[pass]) {
            if (consumed + width > total) return;
            std::memcpy(dst + qint64(y) * width, src + consumed, size_t(width));
            consumed += width;
        }
    }
}

} // namespace

bool GifDecoder::sniff(const QByteArray& head) {
    if (head.size() < 6) return false;
    if (std::memcmp(head.constData(), "GIF8", 4) != 0) return false;
    const char v = head[4];
    return v == '7' || v == '9';
}

MediaResult GifDecoder::decode(const QByteArray& data) {
    MediaResult r;

    if (!sniff(data)) {
        r.error = QStringLiteral("不是 GIF 文件");
        return r;
    }
    if (data.size() < 13) {
        r.error = QStringLiteral("GIF 文件过短");
        return r;
    }

    ByteReader rd(reinterpret_cast<const uchar*>(data.constData()), data.size());
    rd.skip(6);                                   // "GIF87a" / "GIF89a"

    const int canvasW = int(rd.u16());
    const int canvasH = int(rd.u16());
    const uchar packed = rd.byte();
    rd.byte();                                    // background color index（本方案一律用透明背景）
    rd.byte();                                    // pixel aspect ratio

    if (canvasW <= 0 || canvasH <= 0) {
        r.error = QStringLiteral("画布尺寸非法：%1x%2").arg(canvasW).arg(canvasH);
        return r;
    }
    const qint64 canvasPixels = qint64(canvasW) * canvasH;
    if (canvasPixels > kMaxCanvasPixels) {
        r.error = QStringLiteral("画布过大：%1x%2").arg(canvasW).arg(canvasH);
        return r;
    }

    Palette globalPalette;
    if ((packed & 0x80) != 0) {
        const int count = 2 << (packed & 0x07);
        globalPalette.rgb.resize(count * 3);
        globalPalette.size = count;
        if (!rd.read(reinterpret_cast<uchar*>(globalPalette.rgb.data()), count * 3)) {
            r.error = QStringLiteral("全局调色板被截断");
            return r;
        }
    }

    FrameSequence seq;
    seq.width = canvasW;
    seq.height = canvasH;

    CanvasCompositor canvas(canvasW, canvasH);
    canvas.reset();

    // GCE 只对紧随其后的那一帧生效
    int pendingDisposal = 0;
    int pendingDelayCs = 0;
    bool pendingTransparent = false;
    int pendingTransparentIndex = 0;

    while (!rd.atEnd()) {
        const uchar kind = rd.byte();

        if (kind == 0x3B) break;                  // trailer

        if (kind == 0x2C) {                       // image descriptor
            const int ix = int(rd.u16());
            const int iy = int(rd.u16());
            const int iw = int(rd.u16());
            const int ih = int(rd.u16());
            const uchar ipacked = rd.byte();

            if (iw <= 0 || ih <= 0) {
                // 空帧：跳过
                pendingDisposal = 0;
                pendingDelayCs = 0;
                pendingTransparent = false;
                continue;
            }

            Palette palette = globalPalette;
            if ((ipacked & 0x80) != 0) {
                const int count = 2 << (ipacked & 0x07);
                QByteArray rgb(count * 3, '\0');
                if (!rd.read(reinterpret_cast<uchar*>(rgb.data()), count * 3)) {
                    r.error = QStringLiteral("局部调色板被截断");
                    return r;
                }
                palette.rgb = rgb;
                palette.size = count;
            }

            const bool interlaced = (ipacked & 0x40) != 0;
            const uchar minCodeSize = rd.byte();
            const QByteArray lzwData = readSubBlocks(rd);
            if (rd.failed()) { r.error = QStringLiteral("数据流被截断"); return r; }

            QByteArray indices;
            if (!lzwDecode(lzwData, minCodeSize, iw * ih, indices)) {
                r.error = QStringLiteral("LZW 解码失败（第 %1 帧）").arg(seq.frames.size() + 1);
                return r;
            }

            QByteArray ordered;
            if (interlaced) {
                deinterlace(indices, ordered, iw, ih);
            } else {
                ordered = indices;
            }

            // 把索引帧转成 RGBA：透明索引 → alpha 0
            QByteArray rgba(iw * ih * 4, '\0');
            {
                uchar* out = reinterpret_cast<uchar*>(rgba.data());
                const uchar* src = reinterpret_cast<const uchar*>(ordered.constData());
                for (int i = 0; i < iw * ih; ++i) {
                    const uchar v = src[i];
                    uchar* d = out + i * 4;
                    if (pendingTransparent && v == pendingTransparentIndex) {
                        d[3] = 0;                 // 保留底下内容（CanvasCompositor 会跳过）
                        continue;
                    }
                    uchar c[3];
                    fetchColor(palette, v, c);
                    d[0] = c[0];
                    d[1] = c[1];
                    d[2] = c[2];
                    d[3] = 255;
                }
            }

            const QRect frameRect(ix, iy, iw, ih);

            QByteArray backup;
            if (pendingDisposal == 3) backup = canvas.grab(frameRect);

            canvas.blendSource(reinterpret_cast<const uchar*>(rgba.constData()), iw, ih, ix, iy);
            seq.frames.push_back(canvas.snapshot());

            // delay ≤ 1cs 的畸形值兜底为 10cs（浏览器行为）
            double delaySec = pendingDelayCs / 100.0;
            if (pendingDelayCs <= 1) delaySec = 0.10;
            seq.delays.push_back(delaySec);

            switch (pendingDisposal) {
            case 2:
                canvas.clearRect(frameRect);
                break;
            case 3:
                canvas.put(frameRect, backup);
                break;
            default:
                break;
            }

            pendingDisposal = 0;
            pendingDelayCs = 0;
            pendingTransparent = false;
            continue;
        }

        if (kind != 0x21) {
            // 未知块：宁可停在这里也不要继续按猜的方式解析
            break;
        }

        const uchar label = rd.byte();
        if (label == 0xF9) {                      // Graphic Control Extension
            const uchar blockSize = rd.byte();
            if (blockSize < 4) { r.error = QStringLiteral("GCE 长度非法"); return r; }
            const uchar pk = rd.byte();
            const int delayCs = int(rd.u16());
            const uchar transparentIndex = rd.byte();
            rd.skip(blockSize - 4);
            readSubBlocks(rd);

            pendingDisposal = (pk >> 2) & 0x07;
            if (pendingDisposal > 3) pendingDisposal = 0;      // 保留位：按 0 处理
            pendingTransparent = (pk & 0x01) != 0;
            pendingTransparentIndex = transparentIndex;
            pendingDelayCs = delayCs;
            continue;
        }

        if (label == 0xFF) {                      // Application Extension（NETSCAPE2.0 循环次数）
            const uchar blockSize = rd.byte();
            rd.skip(blockSize);
            const QByteArray sub = readSubBlocks(rd);
            if (sub.size() >= 3 && static_cast<uchar>(sub[0]) == 0x01) {
                const int lo = static_cast<uchar>(sub[1]);
                const int hi = static_cast<uchar>(sub[2]);
                seq.loopCount = lo | (hi << 8);   // 0 = 无限循环
            }
            continue;
        }

        readSubBlocks(rd);                        // comment / plain text：忽略
    }

    if (seq.frames.isEmpty()) {
        r.error = QStringLiteral("没有解析到任何帧");
        return r;
    }

    seq.rebuildStarts();
    r.ok = true;
    r.seq = seq;
    return r;
}

MediaResult GifDecoder::decodeFile(const QString& path) {
    MediaResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("打不开文件：%1").arg(path);
        return r;
    }
    const QByteArray data = f.readAll();
    f.close();

    MediaResult decoded = decode(data);
    if (decoded.ok) decoded.seq.sourceName = path;
    return decoded;
}

} // namespace pic2pet
