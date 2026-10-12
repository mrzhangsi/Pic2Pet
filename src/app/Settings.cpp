#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace pic2pet {

namespace {

constexpr int kMaxRecent = 5;

} // namespace

Settings::Settings(const QString& filePath) {
    if (!filePath.isEmpty()) {
        filePath_ = filePath;
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    filePath_ = dir + QStringLiteral("/pic2pet.ini");
}

void Settings::load() {
    QSettings s(filePath_, QSettings::IniFormat);

    data.scalePercent = s.value(QStringLiteral("view/scalePercent"), 100).toInt();
    if (data.scalePercent < kMinScalePercent) data.scalePercent = kMinScalePercent;
    if (data.scalePercent > kMaxScalePercent) data.scalePercent = kMaxScalePercent;

    data.opacity = s.value(QStringLiteral("view/opacity"), 1.0).toDouble();
    if (data.opacity < 0.1) data.opacity = 0.1;
    if (data.opacity > 1.0) data.opacity = 1.0;

    data.alwaysOnTop = s.value(QStringLiteral("window/alwaysOnTop"), true).toBool();
    data.lastMedia = s.value(QStringLiteral("media/last")).toString();
    data.recentMedia = s.value(QStringLiteral("media/recent")).toStringList();

    const auto loadAction = [&s](const char* key, ActionConfig& cfg) {
        const QString p = QStringLiteral("actions/%1/").arg(QString::fromLatin1(key));
        cfg.media = s.value(p + QStringLiteral("media")).toString();
        cfg.audio = s.value(p + QStringLiteral("audio")).toString();
        cfg.holdMs = s.value(p + QStringLiteral("holdMs"), kDefaultHoldMs).toInt();
        if (cfg.holdMs < kMinHoldMs) cfg.holdMs = kMinHoldMs;
        if (cfg.holdMs > kMaxHoldMs) cfg.holdMs = kMaxHoldMs;
    };
    loadAction("idle", data.actionIdle);
    loadAction("click", data.actionClick);
    loadAction("doubleclick", data.actionDoubleClick);
    loadAction("drag", data.actionDrag);
    loadAction("typing", data.actionTyping);

    data.inputMode = s.value(QStringLiteral("input/mode"), false).toBool();
    data.keyBubble = s.value(QStringLiteral("input/keyBubble"), true).toBool();
    data.motionLevel = s.value(QStringLiteral("motion/level"), 2).toInt();
    if (data.motionLevel < 0) data.motionLevel = 0;
    if (data.motionLevel > 3) data.motionLevel = 3;

    data.teacher.load(s);

    const int x = s.value(QStringLiteral("window/x"), -1).toInt();
    const int y = s.value(QStringLiteral("window/y"), -1).toInt();
    if (x >= 0 && y >= 0) data.pos = QPoint(x, y);
}

void Settings::save() {
    QSettings s(filePath_, QSettings::IniFormat);

    s.setValue(QStringLiteral("view/scalePercent"), data.scalePercent);
    s.setValue(QStringLiteral("view/opacity"), data.opacity);
    s.setValue(QStringLiteral("window/alwaysOnTop"), data.alwaysOnTop);
    s.setValue(QStringLiteral("media/last"), data.lastMedia);
    s.setValue(QStringLiteral("media/recent"), data.recentMedia);

    const auto saveAction = [&s](const char* key, const ActionConfig& cfg) {
        const QString p = QStringLiteral("actions/%1/").arg(QString::fromLatin1(key));
        s.setValue(p + QStringLiteral("media"), cfg.media);
        s.setValue(p + QStringLiteral("audio"), cfg.audio);
        s.setValue(p + QStringLiteral("holdMs"), cfg.holdMs);
    };
    saveAction("idle", data.actionIdle);
    saveAction("click", data.actionClick);
    saveAction("doubleclick", data.actionDoubleClick);
    saveAction("drag", data.actionDrag);
    saveAction("typing", data.actionTyping);

    s.setValue(QStringLiteral("input/mode"), data.inputMode);
    s.setValue(QStringLiteral("input/keyBubble"), data.keyBubble);
    s.setValue(QStringLiteral("motion/level"), data.motionLevel);

    data.teacher.save(s);

    s.setValue(QStringLiteral("window/x"), data.pos.x());
    s.setValue(QStringLiteral("window/y"), data.pos.y());
    s.sync();
}

void Settings::rememberMedia(const QString& path) {
    if (path.isEmpty()) return;
    data.lastMedia = path;
    data.recentMedia.removeAll(path);
    data.recentMedia.prepend(path);
    while (data.recentMedia.size() > kMaxRecent) data.recentMedia.removeLast();
}

} // namespace pic2pet
