#include "ActionSet.h"

#include "MediaLoader.h"

#include <QStringList>

namespace pic2pet {

namespace {

/** 加载一段素材为动作片段；loop 由动作语义决定 */
bool loadClip(const QString& path, bool loop, ActionClip* out) {
    const MediaResult r = MediaLoader::loadPath(path);
    if (!r.ok) return false;
    out->seq = r.seq;
    out->loop = loop;
    return true;
}

/** 待机与拖拽循环；单击/双击播完自动回待机 */
bool loopsByDefault(PetAction a) {
    return a == PetAction::Idle || a == PetAction::Drag;
}

} // namespace

QString ActionSet::actionName(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return QStringLiteral("待机");
    case PetAction::Click:       return QStringLiteral("单击");
    case PetAction::DoubleClick: return QStringLiteral("双击");
    case PetAction::Drag:        return QStringLiteral("拖拽");
    }
    return QStringLiteral("?");
}

bool ActionSet::load(const QString& path, QString* error) {
    idle_ = ActionClip();
    click_ = ActionClip();
    doubleClick_ = ActionClip();
    drag_ = ActionClip();
    if (error) error->clear();

    // 只认单个素材文件：它就是这个宠物的待机动作
    ActionClip c;
    if (!loadClip(path, true, &c)) {
        if (error) *error = QStringLiteral("素材加载失败：%1").arg(path);
        return false;
    }
    idle_ = c;
    return true;
}

void ActionSet::ensureIdle() {
    if (idle_.isValid()) return;

    // 没有待机动作 → 用第一个可用动作兜底，避免"有素材却什么都不显示"
    if (click_.isValid()) {
        idle_ = click_;
    } else if (doubleClick_.isValid()) {
        idle_ = doubleClick_;
    } else if (drag_.isValid()) {
        idle_ = drag_;
    }
    if (idle_.isValid()) idle_.loop = true;   // 待机一定循环
}

bool ActionSet::setActionFromPath(PetAction a, const QString& path, int holdMs, QString* error) {
    ActionClip c;
    if (!loadClip(path, loopsByDefault(a), &c)) {
        if (error) *error = QStringLiteral("加载失败：%1").arg(path);
        return false;
    }
    if (holdMs > 0) c.holdMs = holdMs;   // 静态素材的停留时长

    switch (a) {
    case PetAction::Idle:        idle_ = c; break;
    case PetAction::Click:       click_ = c; break;
    case PetAction::DoubleClick: doubleClick_ = c; break;
    case PetAction::Drag:        drag_ = c; break;
    }
    return true;
}

void ActionSet::setHoldMs(PetAction a, int holdMs) {
    if (holdMs <= 0) return;
    switch (a) {
    case PetAction::Idle:        idle_.holdMs = holdMs; break;
    case PetAction::Click:       click_.holdMs = holdMs; break;
    case PetAction::DoubleClick: doubleClick_.holdMs = holdMs; break;
    case PetAction::Drag:        drag_.holdMs = holdMs; break;
    }
}

bool ActionSet::has(PetAction a) const {
    switch (a) {
    case PetAction::Idle:        return idle_.isValid();
    case PetAction::Click:       return click_.isValid();
    case PetAction::DoubleClick: return doubleClick_.isValid();
    case PetAction::Drag:        return drag_.isValid();
    }
    return false;
}

const ActionClip& ActionSet::clip(PetAction a) const {
    switch (a) {
    case PetAction::Click:
        if (click_.isValid()) return click_;
        break;
    case PetAction::DoubleClick:
        if (doubleClick_.isValid()) return doubleClick_;
        break;
    case PetAction::Drag:
        if (drag_.isValid()) return drag_;
        break;
    default:
        break;
    }
    return idle_;   // 缺失一律回退待机
}

qint64 ActionSet::bytes() const {
    qint64 n = 0;
    n += idle_.seq.bytes();
    n += click_.seq.bytes();
    n += doubleClick_.seq.bytes();
    n += drag_.seq.bytes();
    return n;
}

QString ActionSet::summary() const {
    QStringList parts;
    const auto add = [&parts](const char* name, const ActionClip& c) {
        if (c.isValid()) {
            parts << QStringLiteral("%1(%2f)").arg(QString::fromLatin1(name)).arg(c.seq.count());
        }
    };
    add("idle", idle_);
    add("click", click_);
    add("doubleclick", doubleClick_);
    add("drag", drag_);
    return parts.join(QLatin1Char(' '));
}

} // namespace pic2pet
