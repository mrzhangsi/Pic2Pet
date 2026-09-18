#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

namespace pic2pet {

/** 持久化到 INI 的那点偏好 */
struct PetSettingsData {
    QPoint pos;                       // 窗口位置，无效则用右下角默认位置
    int scalePercent = 100;           // 缩放，25~400
    double opacity = 1.0;             // 0.1~1.0
    bool alwaysOnTop = true;
    QString lastMedia;                // 上次加载的素材
    QStringList recentMedia;          // 最近 5 个
};

/**
 * 配置读写。用 IniFormat 而不是注册表/系统默认，
 * 这样配置文件位置可控、排查问题时能直接打开看。
 */
class Settings {
public:
    explicit Settings(const QString& filePath = QString());

    void load();
    void save();

    void rememberMedia(const QString& path);

    PetSettingsData data;
    QString filePath() const { return filePath_; }

private:
    QString filePath_;
};

} // namespace pic2pet
