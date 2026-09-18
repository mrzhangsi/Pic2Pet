#pragma once

#include <QByteArray>
#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * APNG 解码：自研 chunk 分割 + 拼帧 + 交给 QImage 解码。
 *
 * 为什么不直接自己写 inflate：Qt 已内置 PNG 解码，没必要重复实现 IDAT 的
 * zlib 解压、滤波还原、调色板映射那一整套。这里做的只是 APNG 多出来的那部分：
 *   acTL（动画控制：帧数 / 循环次数）
 *   fcTL（每帧控制：尺寸、偏移、delay_num/den、dispose_op、blend_op）
 *   fdAT（帧数据：去掉头部 4 字节序号后就是 IDAT 语义）
 *
 * 做法：把每一帧重新拼成一个「合法的独立 PNG 字节流」交给 QImage::loadFromData，
 * 然后用与 GIF 共用的 CanvasCompositor 完成 blend / dispose。
 */
class ApngDecoder {
public:
    /** 是否 PNG（只看签名） */
    static bool sniffPng(const QByteArray& head);

    /** 是否动画 PNG：在 IDAT 之前存在 acTL */
    static bool isAnimatedPng(const QByteArray& data);

    static MediaResult decode(const QByteArray& data);
    static MediaResult decodeFile(const QString& path);
};

} // namespace pic2pet
