#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>

namespace pic2pet {

/**
 * 命中区域集合（逻辑坐标，相对窗口左上角）。
 *
 * 移植自 Petra 的 InteractiveRegion：用矩形列表而不是逐像素 alpha mask，
 * 开销极低且支持「并集」语义（多个不相交区域各自生效，而非合并成包围盒）。
 * M2 会由角色包围盒自动生成；M0 用一个居中的矩形代替。
 */
class HitRegions {
public:
    void clear();
    void add(const QRect& rect, bool enabled = true);
    void setEnabled(int index, bool enabled);

    int count() const { return items_.size(); }
    bool isEmpty() const { return items_.isEmpty(); }

    /** 命中则返回 true（只统计 enabled 的区域，是并集不是包围盒） */
    bool hit(const QPoint& windowLocalPos) const;

    QRect boundingBox() const;

private:
    struct Item {
        QRect rect;
        bool enabled = true;
    };
    QVector<Item> items_;
};

} // namespace pic2pet
