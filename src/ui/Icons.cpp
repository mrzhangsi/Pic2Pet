#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace pic2pet::theme {

namespace {

constexpr qreal kDesign = 16.0;   // 图标按 16x16 设计

QPen strokePen(const QColor& color, qreal width) {
    QPen pen(color);
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCosmetic(false);
    return pen;
}

void paintMedia(QPainter& p, const QPen& pen, const QColor& c) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.0, 3.0, 12.0, 10.0), 2.0, 2.0);   // 相片外框
    p.setBrush(c);
    p.drawEllipse(QPointF(6.0, 6.6), 1.1, 1.1);                   // 太阳
    p.setBrush(Qt::NoBrush);
    QPainterPath hill;                                            // 山形
    hill.moveTo(3.2, 12.0);
    hill.lineTo(6.4, 8.6);
    hill.lineTo(8.8, 10.8);
    hill.lineTo(11.0, 9.2);
    hill.lineTo(12.8, 12.0);
    p.drawPath(hill);
}

void paintAction(QPainter& p, const QPen& pen, const QColor& c) {
    Q_UNUSED(c);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath tri;                                             // 播放三角
    tri.moveTo(5.2, 3.6);
    tri.lineTo(12.6, 8.0);
    tri.lineTo(5.2, 12.4);
    tri.closeSubpath();
    p.drawPath(tri);
}

void paintInput(QPainter& p, const QPen& pen, const QColor& c) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(1.4, 4.0, 13.2, 8.0), 2.0, 2.0);     // 键盘外框
    p.setBrush(c);
    for (int i = 0; i < 4; ++i) {                                 // 一排键帽
        p.drawRoundedRect(QRectF(3.6 + i * 2.3, 6.3, 1.3, 1.3), 0.35, 0.35);
    }
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(5.6, 9.9), QPointF(10.4, 9.9));            // 空格
}

void paintMotion(QPainter& p, const QPen& pen, const QColor& c) {
    Q_UNUSED(c);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath wave;                                            // 波形（呼吸/弹动）
    wave.moveTo(1.8, 10.2);
    wave.lineTo(4.9, 5.8);
    wave.lineTo(8.0, 10.2);
    wave.lineTo(11.1, 5.8);
    wave.lineTo(14.2, 10.2);
    p.drawPath(wave);
}

void paintAppearance(QPainter& p, const QPen& pen, const QColor& c) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(8.0, 8.0), 5.6, 5.6);                   // 明暗/不透明度
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPie(QRectF(2.4, 2.4, 11.2, 11.2), 90 * 16, 180 * 16);   // 左半边填实
}

void paintGeneral(QPainter& p, const QPen& pen, const QColor& c) {
    Q_UNUSED(c);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(8.0, 8.0), 4.6, 4.6);                   // 齿轮外圈
    p.drawEllipse(QPointF(8.0, 8.0), 1.9, 1.9);                   // 轴心
    for (int i = 0; i < 6; ++i) {                                 // 六根齿
        const double a = i * 3.14159265358979 / 3.0;
        const QPointF inner(8.0 + std::cos(a) * 4.6, 8.0 + std::sin(a) * 4.6);
        const QPointF outer(8.0 + std::cos(a) * 6.4, 8.0 + std::sin(a) * 6.4);
        p.drawLine(inner, outer);
    }
}

} // namespace

void paintIcon(QPainter& p, Icon icon, const QRectF& box, const QColor& color,
               qreal strokeWidth) {
    const qreal side = std::min(box.width(), box.height());
    if (side <= 0.0) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    // 平移到 box 中心，再缩放到 16x16 的设计坐标系（居中、等比）
    p.translate(box.center());
    const qreal scale = side / kDesign;
    p.scale(scale, scale);
    p.translate(-kDesign / 2.0, -kDesign / 2.0);

    const QPen pen = strokePen(color, strokeWidth / scale);

    // 让描边不要溢出设计框
    p.setClipRect(QRectF(-0.5, -0.5, kDesign + 1.0, kDesign + 1.0));

    switch (icon) {
    case Icon::Media:      paintMedia(p, pen, color); break;
    case Icon::Action:     paintAction(p, pen, color); break;
    case Icon::Input:      paintInput(p, pen, color); break;
    case Icon::Motion:     paintMotion(p, pen, color); break;
    case Icon::Appearance: paintAppearance(p, pen, color); break;
    case Icon::General:    paintGeneral(p, pen, color); break;
    }
    p.restore();
}

} // namespace pic2pet::theme
