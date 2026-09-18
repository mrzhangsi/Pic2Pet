#include "ApngDecoder.h"

#include "CanvasCompositor.h"

#include <QFile>
#include <QImage>

#include <cstring>

namespace pic2pet {

namespace {

const uchar kPngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

quint32 gCrcTable[256];
bool gCrcReady = false;

void buildCrcTable() {
    if (gCrcReady) return;
    for (quint32 n = 0; n < 256; ++n) {
        quint32 c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        gCrcTable[n] = c;
    }
    gCrcReady = true;
}

/** PNG 用的 CRC32（以太网多项式，与 zlib 一致） */
quint32 crc32Of(const uchar* data, size_t len) {
    buildCrcTable();
    quint32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = gCrcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

quint32 readBigEndianU32(const uchar* p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

void writeBigEndianU32(uchar* p, quint32 v) {
    p[0] = uchar((v >> 24) & 0xFF);
    p[1] = uchar((v >> 16) & 0xFF);
    p[2] = uchar((v >> 8) & 0xFF);
    p[3] = uchar(v & 0xFF);
}

void appendChunk(QByteArray& out, const char type[4], const QByteArray& data) {
    quint32 len = quint32(data.size());
    uchar lenBytes[4];
    writeBigEndianU32(lenBytes, len);
    out.append(reinterpret_cast<const char*>(lenBytes), 4);

    // 参与 CRC 计算的区段是「类型 + 数据」
    QByteArray crcInput(type, 4);
    crcInput.append(data);
    const quint32 crc = crc32Of(reinterpret_cast<const uchar*>(crcInput.constData()),
                                size_t(crcInput.size()));

    out.append(type, 4);
    out.append(data);

    uchar crcBytes[4];
    writeBigEndianU32(crcBytes, crc);
    out.append(reinterpret_cast<const char*>(crcBytes), 4);
}

struct Chunk {
    QByteArray type;
    QByteArray data;
};

/** 把 PNG 拆成一个个 chunk；失败返回 false */
bool splitChunks(const QByteArray& data, QVector<Chunk>& out, int& canvasW, int& canvasH) {
    out.clear();
    if (data.size() < 8) return false;
    if (std::memcmp(data.constData(), kPngSignature, 8) != 0) return false;

    qint64 pos = 8;
    const qint64 total = data.size();
    canvasW = canvasH = 0;

    while (pos + 8 <= total) {
        const uchar* p = reinterpret_cast<const uchar*>(data.constData()) + pos;
        const quint32 len = readBigEndianU32(p);

        Chunk c;
        c.type = data.mid(int(pos) + 4, 4);
        if (pos + 8 + qint64(len) > total) return false;
        c.data = data.mid(int(pos) + 8, int(len));
        out.push_back(c);

        if (c.type == "IHDR") {
            if (c.data.size() < 13) return false;
            const uchar* ih = reinterpret_cast<const uchar*>(c.data.constData());
            canvasW = int(readBigEndianU32(ih));
            canvasH = int(readBigEndianU32(ih + 4));
        }

        pos += 8 + qint64(len);
        if (c.type == "IEND") break;
    }
    return canvasW > 0 && canvasH > 0;
}

struct ApngFrame {
    bool fromIdat = false;          // true = 数据来自默认图像 IDAT
    bool emitted = true;            // 默认图像若为隐藏帧则不参与动画
    int width = 0, height = 0;
    int x = 0, y = 0;
    quint16 delayNum = 0, delayDen = 0;
    uchar disposeOp = 0;
    uchar blendOp = 0;
    QByteArray payload;
};

} // namespace

bool ApngDecoder::sniffPng(const QByteArray& head) {
    if (head.size() < 8) return false;
    return std::memcmp(head.constData(), kPngSignature, 8) == 0;
}

bool ApngDecoder::isAnimatedPng(const QByteArray& data) {
    QVector<Chunk> chunks;
    int w = 0, h = 0;
    if (!splitChunks(data, chunks, w, h)) return false;
    for (const Chunk& c : chunks) {
        if (c.type == "acTL") return true;
        if (c.type == "IDAT") return false;     // acTL 必须出现在 IDAT 之前
    }
    return false;
}

MediaResult ApngDecoder::decode(const QByteArray& data) {
    MediaResult r;

    QVector<Chunk> chunks;
    int canvasW = 0, canvasH = 0;
    if (!splitChunks(data, chunks, canvasW, canvasH)) {
        r.error = QStringLiteral("不是合法的 PNG");
        return r;
    }

    QByteArray ihdr;
    QVector<ApngFrame> frames;
    int loopCount = 0;
    bool sawIdat = false;

    for (const Chunk& c : chunks) {
        if (c.type == "IHDR") {
            ihdr = c.data;
            continue;
        }
        if (c.type == "acTL") {
            if (c.data.size() >= 8) {
                const uchar* p = reinterpret_cast<const uchar*>(c.data.constData());
                loopCount = int(readBigEndianU32(p + 4));
            }
            continue;
        }
        if (c.type == "fcTL") {
            ApngFrame f;
            if (c.data.size() >= 26) {
                const uchar* p = reinterpret_cast<const uchar*>(c.data.constData());
                f.width = int(readBigEndianU32(p + 4));
                f.height = int(readBigEndianU32(p + 8));
                f.x = int(readBigEndianU32(p + 12));
                f.y = int(readBigEndianU32(p + 16));
                f.delayNum = quint16((p[20] << 8) | p[21]);
                f.delayDen = quint16((p[22] << 8) | p[23]);
                f.disposeOp = p[24];
                f.blendOp = p[25];
            }
            frames.push_back(f);
            continue;
        }
        if (c.type == "IDAT") {
            sawIdat = true;
            if (!frames.isEmpty()) {
                // 该帧的数据就是默认图像的 IDAT（可能分成多个块）
                ApngFrame& f = frames[frames.size() - 1];
                f.fromIdat = true;
                f.payload.append(c.data);
            }
            continue;
        }
        if (c.type == "fdAT") {
            if (frames.isEmpty()) continue;
            ApngFrame& f = frames[frames.size() - 1];
            if (c.data.size() > 4) {
                f.payload.append(c.data.mid(4));    // 去掉 4 字节帧序号
            }
            continue;
        }
        if (c.type == "IEND") break;
        // 其余块（PLTE / tRNS 等）单独在下面重扫时按类型取用
    }

    if (frames.isEmpty()) {
        r.error = QStringLiteral("这不是动画 PNG（没有 fcTL）");
        return r;
    }

    // 上面的 framingChunks 只保留了数据部分，丢掉了类型；这里改为直接重扫一遍，
    // 只挑出 PNG 解码必需的 PLTE / tRNS 两种，顺序固定（PLTE 必须在 tRNS 之前）。
    QByteArray paletteData;
    QByteArray transparencyData;
    for (const Chunk& c : chunks) {
        if (c.type == "IDAT" || c.type == "fdAT") break;
        if (c.type == "PLTE") paletteData = c.data;
        if (c.type == "tRNS") transparencyData = c.data;
    }

    FrameSequence seq;
    seq.width = canvasW;
    seq.height = canvasH;
    seq.loopCount = loopCount;

    CanvasCompositor canvas(canvasW, canvasH);
    canvas.reset();

    for (int i = 0; i < frames.size(); ++i) {
        ApngFrame& f = frames[i];

        // 隐藏的默认图像（fcTL 出现在 IDAT 之后且帧数据来自 fdAT）不参与动画
        if (f.payload.isEmpty()) continue;

        const int fw = (f.width > 0) ? f.width : canvasW;
        const int fh = (f.height > 0) ? f.height : canvasH;

        // 重造一份独立的 PNG：签名 + IHDR(改成这一帧的尺寸) + PLTE/tRNS + 该帧数据 + IEND
        QByteArray frameIhdr = ihdr;
        if (frameIhdr.size() >= 13) {
            uchar* p = reinterpret_cast<uchar*>(frameIhdr.data());
            writeBigEndianU32(p, quint32(fw));
            writeBigEndianU32(p + 4, quint32(fh));
        }

        QByteArray png;
        png.append(reinterpret_cast<const char*>(kPngSignature), 8);
        appendChunk(png, "IHDR", frameIhdr);
        if (!paletteData.isEmpty()) appendChunk(png, "PLTE", paletteData);
        if (!transparencyData.isEmpty()) appendChunk(png, "tRNS", transparencyData);
        appendChunk(png, "IDAT", f.payload);
        appendChunk(png, "IEND", QByteArray());

        QImage img;
        if (!img.loadFromData(png, "PNG")) {
            r.error = QStringLiteral("第 %1 帧解码失败").arg(i + 1);
            return r;
        }

        QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) {
            r.error = QStringLiteral("第 %1 帧格式转换失败").arg(i + 1);
            return r;
        }

        QByteArray frameBits(reinterpret_cast<const char*>(rgba.constBits()),
                             int(rgba.sizeInBytes()));

        const QRect frameRect(f.x, f.y, rgba.width(), rgba.height());
        QByteArray backup;
        if (f.disposeOp == 2) backup = canvas.grab(frameRect);

        if (f.blendOp == 0) {
            canvas.blendSource(reinterpret_cast<const uchar*>(frameBits.constData()),
                               rgba.width(), rgba.height(), f.x, f.y);
        } else {
            canvas.blendOver(reinterpret_cast<const uchar*>(frameBits.constData()),
                             rgba.width(), rgba.height(), f.x, f.y);
        }

        seq.frames.push_back(canvas.snapshot());

        double delaySec = 0.1;
        if (f.delayDen > 0) delaySec = double(f.delayNum) / double(f.delayDen);
        else if (f.delayNum > 0) delaySec = double(f.delayNum) / 100.0;
        if (delaySec <= 0.0) delaySec = 0.1;
        seq.delays.push_back(delaySec);

        switch (f.disposeOp) {
        case 1:
            canvas.clearRect(frameRect);
            break;
        case 2:
            canvas.put(frameRect, backup);
            break;
        default:
            break;
        }
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

MediaResult ApngDecoder::decodeFile(const QString& path) {
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
