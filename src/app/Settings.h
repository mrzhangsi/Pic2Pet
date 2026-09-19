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

/** 静态素材的"停留时长"范围（毫秒）。action 是一次性动作时，显示这么久再回待机。 */
constexpr int kMinHoldMs = 50;
constexpr int kMaxHoldMs = 60000;
constexpr int kDefaultHoldMs = 600;

/**
 * 单个动作的配置。**音频与素材完全独立**：可以只配音频不配素材，反之亦然。
 */
struct ActionConfig {
    QString media;                        // 素材文件（图片/动图）；空 = 该动作没有素材
    QString audio;                        // 音频文件；空 = 不发声
    int holdMs = kDefaultHoldMs;          // media 是**静态图**时的停留时长（毫秒）
};

/** 持久化到 INI 的那点偏好 */
struct PetSettingsData {
    QPoint pos;                       // 窗口位置，无效则用右下角默认位置
    int scalePercent = 100;           // 缩放，kMinScalePercent~kMaxScalePercent
    double opacity = 1.0;             // 0.1~1.0
    bool alwaysOnTop = true;
    QString lastMedia;                // 当前素材文件（也是待机动作的默认素材）
    QStringList recentMedia;          // 最近 5 个

    ActionConfig actionIdle;          // 待机：media 为空时用 lastMedia
    ActionConfig actionClick;
    ActionConfig actionDoubleClick;
    ActionConfig actionDrag;
    ActionConfig actionTyping;        // 输入模式：敲键盘时触发

    /**
     * "输入模式"：全局监听键盘，你敲键时桌宠跟着动（触发 actionTyping）。
     * **默认 false** —— 这是全项目唯一的挂钩式能力，必须用户显式打开，
     * 且会加重杀软误报（见 src/input/README.md 与 README 的「被报毒」一节）。
     */
    bool inputMode = false;
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
