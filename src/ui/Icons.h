#pragma once

#include <QColor>
#include <QRectF>

class QPainter;

namespace pic2pet::theme {

/** 侧边栏用的极简图标。全部**自绘矢量描边**：零依赖、跟着主题变色、放大不糊。 */
enum class Icon {
    Media,        // 素材：相片
    Action,       // 动作：播放
    Input,        // 输入：键盘
    Motion,       // 动态效果：波形
    Appearance,   // 外观：明暗
    General,      // 通用：齿轮
};

/**
 * 在 box 内绘制图标。
 * 图标本身按 16x16 设计，这里会等比缩放到 box（保证正方形，居中）。
 */
void paintIcon(QPainter& p, Icon icon, const QRectF& box, const QColor& color,
               qreal strokeWidth = 1.5);

} // namespace pic2pet::theme
