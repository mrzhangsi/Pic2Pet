#pragma once

#include <QByteArray>
#include <QString>

#include "FrameSequence.h"

namespace pic2pet {

/**
 * 自研 GIF 解码器（技术方案 12.1：M1 不引入第三方库）。
 *
 * 为什么必须自研：Qt 的 QImageReader/QMovie 不暴露 disposal 语义之外的合成控制，
 * 而且逐帧解码无法做到"播放期零解码"。
 *
 * 残影的唯一来源就是 disposal 处理，这里严格按规范实现：
 *   0 / 未指定 → 视同 1（保留）
 *   1          → 保留画布内容
 *   2          → 输出后把该帧矩形清为透明
 *   3          → 输出后恢复该帧矩形到绘制前的备份
 *
 * 另外几个必须踩对的点：
 *   - delay ≤ 1cs 的畸形值兜底为 10cs，否则浏览器级素材会闪成幻灯片
 *   - 局部调色板优先于全局调色板
 *   - interlace 标志要做 4-pass 解交织
 *   - 帧矩形可能越界，需裁剪
 */
class GifDecoder {
public:
    /** 只看文件头，判断是不是 GIF */
    static bool sniff(const QByteArray& head);

    /** 解码内存中的 GIF 数据 */
    static MediaResult decode(const QByteArray& data);

    /** 读文件并解码 */
    static MediaResult decodeFile(const QString& path);
};

} // namespace pic2pet
