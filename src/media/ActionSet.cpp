#include "ActionSet.h"

#include "MediaLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace pic2pet {

namespace {

struct NameMap {
    const char* key;
    PetAction action;
};

// 别名表：文件名（去掉扩展名）或子目录名 → 动作（英文，比较时转小写）
const NameMap kNameMap[] = {
    { "idle", PetAction::Idle },
    { "stand", PetAction::Idle },
    { "wait", PetAction::Idle },

    { "click", PetAction::Click },
    { "tap", PetAction::Click },

    { "doubleclick", PetAction::DoubleClick },
    { "dblclick", PetAction::DoubleClick },

    { "drag", PetAction::Drag },
    { "dragging", PetAction::Drag },
};

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

bool ActionSet::actionFromName(const QString& baseName, PetAction* out) {
    if (baseName.isEmpty() || !out) return false;

    const QString lower = baseName.toLower();
    for (const NameMap& m : kNameMap) {
        if (lower == QLatin1String(m.key)) {
            *out = m.action;
            return true;
        }
    }

    // 中文别名
    if (baseName == QStringLiteral("待机")) {
        *out = PetAction::Idle;
        return true;
    }
    if (baseName == QStringLiteral("点击") || baseName == QStringLiteral("单击")) {
        *out = PetAction::Click;
        return true;
    }
    if (baseName == QStringLiteral("双击")) {
        *out = PetAction::DoubleClick;
        return true;
    }
    if (baseName == QStringLiteral("拖拽") || baseName == QStringLiteral("拖动")) {
        *out = PetAction::Drag;
        return true;
    }
    return false;
}

bool ActionSet::load(const QString& path, QString* error) {
    idle_ = ActionClip();
    click_ = ActionClip();
    doubleClick_ = ActionClip();
    drag_ = ActionClip();
    if (error) error->clear();

    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (error) *error = QStringLiteral("路径不存在：%1").arg(path);
        return false;
    }

    const auto put = [this](PetAction a, const ActionClip& c) {
        switch (a) {
        case PetAction::Idle:        idle_ = c; break;
        case PetAction::Click:       click_ = c; break;
        case PetAction::DoubleClick: doubleClick_ = c; break;
        case PetAction::Drag:        drag_ = c; break;
        }
    };

    if (fi.isDir()) {
        const QDir dir(path);
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

        QString firstErr;
        int matched = 0;
        for (const QFileInfo& e : entries) {
            const QString base = e.isDir() ? e.fileName() : e.completeBaseName();
            if (base.startsWith(QLatin1Char('.'))) continue;   // 跳过隐藏项

            PetAction a;
            if (!actionFromName(base, &a)) continue;

            ActionClip c;
            if (loadClip(e.absoluteFilePath(), loopsByDefault(a), &c)) {
                put(a, c);
                ++matched;
            } else if (firstErr.isEmpty()) {
                firstErr = QStringLiteral("动作 %1 加载失败").arg(base);
            }
        }

        if (matched == 0) {
            // 目录里一个约定名都没认出来 → 整份当一条序列（M1 行为，向后兼容）
            ActionClip c;
            if (!loadClip(path, true, &c)) {
                if (error) {
                    *error = firstErr.isEmpty() ? QStringLiteral("目录里没有可用素材") : firstErr;
                }
                return false;
            }
            idle_ = c;
        }
    } else {
        // 单个文件 → 只当待机动作
        ActionClip c;
        if (!loadClip(path, true, &c)) {
            if (error) *error = QStringLiteral("素材加载失败：%1").arg(path);
            return false;
        }
        idle_ = c;
    }

    ensureIdle();

    if (!idle_.isValid()) {
        if (error) *error = QStringLiteral("没有可播放的动作");
        return false;
    }
    return true;
}

void ActionSet::ensureIdle() {
    if (idle_.isValid()) return;

    // 没有待机动作 → 用第一个可用动作兜底，避免"加载成功却什么都不显示"
    if (click_.isValid()) {
        idle_ = click_;
    } else if (doubleClick_.isValid()) {
        idle_ = doubleClick_;
    } else if (drag_.isValid()) {
        idle_ = drag_;
    }
    if (idle_.isValid()) idle_.loop = true;   // 待机一定循环
}

bool ActionSet::setActionFromPath(PetAction a, const QString& path, QString* error) {
    ActionClip c;
    if (!loadClip(path, loopsByDefault(a), &c)) {
        if (error) *error = QStringLiteral("加载失败：%1").arg(path);
        return false;
    }

    switch (a) {
    case PetAction::Idle:        idle_ = c; break;
    case PetAction::Click:       click_ = c; break;
    case PetAction::DoubleClick: doubleClick_ = c; break;
    case PetAction::Drag:        drag_ = c; break;
    }
    return true;
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
