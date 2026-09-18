#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

namespace pic2pet {

/**
 * 缩放范围（%）。
 * 注意：窗口尺寸按**上限**固定预留，缩放时永不 resize —— 这是"完全不闪"的前提，
 * 所以这个上限直接决定窗口大小与常驻内存（200% → 默认基准 360 时窗口固定 720²）。
 */
constexpr int kMinScalePercent = 25;
constexpr int kMaxScalePercent = 200;

/** 持久化到 INI 的那点偏好 */
struct PetSettingsData {
    QPoint pos;                       // 窗口位置，无效则用右下角默认位置
    int scalePercent = 100;           // 缩放，kMinScalePercent~kMaxScalePercent
    double opacity = 1.0;             // 0.1~1.0
    bool alwaysOnTop = true;
    QString lastMedia;                // 上次加载的素材文件
    QStringList recentMedia;          // 最近 5 个

    // 每个动作的"指定素材文件"。留空 = 该动作没有素材（不会播放）
    QString actionIdle;
    QString actionClick;
    QString actionDoubleClick;
    QString actionDrag;
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
