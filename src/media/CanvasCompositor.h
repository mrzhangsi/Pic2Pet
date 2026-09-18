#pragma once

#include <QByteArray>
#include <QRect>

namespace pic2pet {

/**
 * 帧合成器：把「增量帧」按 blend / dispose 规则合成为「全画布帧」。
 *
 * GIF 和 APNG 的合成语义是同构的（dispose 都是 none / background / previous 三种，
 * blend 都是 source / over 两种），所以这里只写一套，两个解码器共用。
 *
 * 关于 disposal=2 的清屏颜色（重要）：
 *   规范写的是"恢复为背景色"，但桌宠的背景就是桌面，刷成白色会得到一块刺眼的方块。
 *   因此这里**一律清为透明**。这也是 PIL / 浏览器在透明 GIF 上的实际行为。
 */
class CanvasCompositor {
public:
    CanvasCompositor(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    /** 整块画布清为透明 */
    void reset();

    /**
     * blend_op = SOURCE：把 src 贴到 (dx, dy)。
     * 逐像素：src alpha == 0 的像素**跳过**（保留底下已有内容），
     * 这正是 GIF 透明索引的语义。
     */
    void blendSource(const uchar* src, int sw, int sh, int dx, int dy);

    /** blend_op = OVER：常规 alpha 合成（APNG blend_op=1 用） */
    void blendOver(const uchar* src, int sw, int sh, int dx, int dy);

    /** dispose_op = BACKGROUND：清为透明 */
    void clearRect(const QRect& r);

    /** dispose_op = PREVIOUS 用：绘制前备份 */
    QByteArray grab(const QRect& r) const;

    /** dispose_op = PREVIOUS 用：恢复到备份 */
    void put(const QRect& r, const QByteArray& buf);

    /** 取当前整张画布（把这一帧固化进 FrameSequence） */
    QByteArray snapshot() const { return buffer_; }

private:
    // 把逻辑矩形裁剪到画布内，返回裁剪后的矩形；若完全在画布外返回无效矩形
    QRect clip(const QRect& r) const;

    int width_ = 0;
    int height_ = 0;
    QByteArray buffer_;
};

} // namespace pic2pet
