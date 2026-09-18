#include "HitRegions.h"

namespace pic2pet {

void HitRegions::clear() { items_.clear(); }

void HitRegions::add(const QRect& rect, bool enabled) {
    items_.push_back(Item{rect, enabled});
}

void HitRegions::setEnabled(int index, bool enabled) {
    if (index < 0 || index >= items_.size()) return;
    items_[index].enabled = enabled;
}

bool HitRegions::hit(const QPoint& windowLocalPos) const {
    for (const Item& item : items_) {
        if (item.enabled && item.rect.contains(windowLocalPos)) return true;
    }
    return false;
}

QRect HitRegions::boundingBox() const {
    if (items_.isEmpty()) return QRect();
    QRect box;
    for (const Item& item : items_) box = box.united(item.rect);
    return box;
}

} // namespace pic2pet
