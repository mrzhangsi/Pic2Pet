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
    if (data.scalePercent < 25) data.scalePercent = 25;
    if (data.scalePercent > 400) data.scalePercent = 400;

    data.opacity = s.value(QStringLiteral("view/opacity"), 1.0).toDouble();
    if (data.opacity < 0.1) data.opacity = 0.1;
    if (data.opacity > 1.0) data.opacity = 1.0;

    data.alwaysOnTop = s.value(QStringLiteral("window/alwaysOnTop"), true).toBool();
    data.lastMedia = s.value(QStringLiteral("media/last")).toString();
    data.recentMedia = s.value(QStringLiteral("media/recent")).toStringList();

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
