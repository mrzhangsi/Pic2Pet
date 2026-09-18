#include "AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace pic2pet {

namespace {

constexpr const char* kValueName = "Pic2Pet";
constexpr const char* kBundleId = "com.pic2pet.app";

QString appPath() {
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

#ifdef Q_OS_WIN
constexpr bool kSupported = true;
#elif defined(Q_OS_MACOS)
constexpr bool kSupported = true;
#elif defined(Q_OS_LINUX)
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

} // namespace

bool AutoStart::supported() { return kSupported; }

bool AutoStart::isEnabled() {
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kValueName));
#elif defined(Q_OS_MACOS)
    const QString dir = QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
    return QFile::exists(dir + QLatin1Char('/') + QLatin1String(kBundleId) + QStringLiteral(".plist"));
#elif defined(Q_OS_LINUX)
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/autostart");
    return QFile::exists(dir + QStringLiteral("/pic2pet.desktop"));
#else
    return false;
#endif
}

bool AutoStart::setEnabled(bool on) {
    if (!supported()) return false;

#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (on) {
        run.setValue(QString::fromLatin1(kValueName), QLatin1Char('"') + appPath() + QLatin1Char('"'));
    } else {
        run.remove(QString::fromLatin1(kValueName));
    }
    run.sync();
    return run.status() == QSettings::NoError;
#elif defined(Q_OS_MACOS)
    const QString dir = QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
    const QString file = dir + QLatin1Char('/') + QLatin1String(kBundleId) + QStringLiteral(".plist");
    if (!on) {
        return QFile::remove(file) || !QFile::exists(file);
    }
    QDir().mkpath(dir);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
           " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "  <dict>\n"
        << "    <key>Label</key>\n    <string>" << kBundleId << "</string>\n"
        << "    <key>ProgramArguments</key>\n    <array>\n      <string>"
        << appPath() << "</string>\n    </array>\n"
        << "    <key>RunAtLoad</key>\n    <true/>\n"
        << "  </dict>\n"
        << "</plist>\n";
    f.close();
    return true;
#elif defined(Q_OS_LINUX)
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/autostart");
    const QString file = dir + QStringLiteral("/pic2pet.desktop");
    if (!on) {
        return QFile::remove(file) || !QFile::exists(file);
    }
    QDir().mkpath(dir);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Pic2Pet\n"
        << "Exec=" << appPath() << "\n"
        << "Hidden=false\n"
        << "NoDisplay=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    f.close();
    return true;
#else
    Q_UNUSED(on);
    return false;
#endif
}

} // namespace pic2pet
