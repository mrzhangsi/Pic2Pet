#include "AudioPlayer.h"

#include <QDebug>
#include <QDir>
#include <QStringList>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <mmsystem.h>

#  include <string>
#endif

namespace pic2pet {

#ifdef Q_OS_WIN

namespace {

// MCI 的 alias 是进程级的，固定一个即可
const char* const kAlias = "pic2pet_audio";

QString mciErrorText(MCIERROR code) {
    if (code == 0) return QString();
    wchar_t buf[256] = { 0 };
    if (mciGetErrorStringW(code, buf, 256)) return QString::fromWCharArray(buf);
    return QStringLiteral("MCI error %1").arg(static_cast<uint>(code));
}

/** 发一条 MCI 命令；失败时把系统错误文本写进 *err */
bool sendMci(const QString& command, QString* err) {
    const std::wstring wide = command.toStdWString();
    const MCIERROR rc = mciSendStringW(wide.c_str(), nullptr, 0, nullptr);
    if (rc != 0) {
        if (err) *err = mciErrorText(rc);
        return false;
    }
    return true;
}

class MciAudioPlayer : public AudioPlayer {
public:
    QString name() const override { return QStringLiteral("Win32 MCI (winmm)"); }

    bool play(const QString& path) override {
        stop();
        if (path.isEmpty()) return true;   // 该动作没配音频

        const QString native = QDir::toNativeSeparators(path);
        const QString alias = QString::fromLatin1(kAlias);

        // MCI 的 open 有时认不出扩展名，所以按"自动识别 → mpegvideo → waveaudio"兜三次
        const QStringList attempts = {
            QStringLiteral("open \"%1\" alias %2").arg(native, alias),
            QStringLiteral("open \"%1\" type mpegvideo alias %2").arg(native, alias),
            QStringLiteral("open \"%1\" type waveaudio alias %2").arg(native, alias),
        };

        QString lastErr;
        bool opened = false;
        for (const QString& cmd : attempts) {
            if (sendMci(cmd, &lastErr)) {
                opened = true;
                break;
            }
        }
        if (!opened) {
            qWarning().noquote() << "[audio] 打不开音频：" << path << "（" << lastErr << "）";
            return false;
        }

        if (!sendMci(QStringLiteral("play %1").arg(alias), &lastErr)) {
            qWarning().noquote() << "[audio] 播放失败：" << path << "（" << lastErr << "）";
            closeDevice();
            return false;
        }

        opened_ = true;
        return true;
    }

    void stop() override {
        if (!opened_) return;
        closeDevice();
    }

private:
    void closeDevice() {
        const QString alias = QString::fromLatin1(kAlias);
        sendMci(QStringLiteral("stop %1").arg(alias), nullptr);
        sendMci(QStringLiteral("close %1").arg(alias), nullptr);
        opened_ = false;
    }

    bool opened_ = false;
};

#else

class NullAudioPlayer : public AudioPlayer {
public:
    QString name() const override { return QStringLiteral("none (platform stub)"); }
    bool play(const QString& path) override {
        Q_UNUSED(path);
        return true;
    }
    void stop() override {}
};

#endif

AudioPlayer& audioPlayer() {
#ifdef Q_OS_WIN
    static MciAudioPlayer player;
#else
    static NullAudioPlayer player;
#endif
    return player;
}

} // namespace pic2pet
