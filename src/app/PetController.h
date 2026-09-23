#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>

#include "ActionSet.h"
#include "ClickThrough.h"
#include "FrameSequence.h"
#include "HitRegions.h"
#include "PerfProbe.h"
#include "PetMotion.h"
#include "Settings.h"

class QMenu;
class QSystemTrayIcon;
class QTimer;

namespace pic2pet {

class ActionSettingsDialog;
class KeyBubble;
class KeyboardMonitor;
class PetWindow;
class SettingsWindow;

/**
 * 装配中心：把窗口、渲染、素材播放、穿透调度、配置与性能采样串起来。
 *
 * 穿透调度刻意跑在主线程（16ms 定时器），原因：
 *   - Qt 的 setWindowFlag() 只能在主线程调用；
 *   - QCursor::pos() 一次只有微秒级开销，开独立线程收益不大、反而引入同步复杂度。
 *
 * M1 的三重短路（技术方案 12.2），全部集中在这里和 PetRenderer 里：
 *   ① 静态图：上传一次后直接停掉动画定时器
 *   ② 帧序号没变：PetRenderer::showFrame() 直接 return，不触发 update()
 *   ③ 窗口不可见：这里停掉所有定时器（靠 eventFilter 监听 Show/Hide）
 */
class PetController : public QObject {
    Q_OBJECT
public:
    PetController(ClickThroughBackend backend, int size, bool useTray, QObject* parent = nullptr);
    ~PetController() override;

    /**
     * 启动。cliMedia 非空则加载它，否则回退到上次记住的素材。
     * 两者只走一条路径，避免素材被加载两次。
     */
    void start(const QString& cliMedia = QString());
    void switchBackend(ClickThroughBackend backend);
    QString statusLine() const;

    /** 抓取当前渲染结果（离屏，不依赖窗口是否可见），用于自动化验证渲染路径 */
    bool grabFramebuffer(const QString& path);

    /** 打开新版「设置」窗口（左侧边栏 + 分页）；page 用来直接跳到某一页（开发/截图用） */
    void openSettings(int page = 0);

    /** 打开"动作设置"窗口（按需创建、关闭即释放） */
    void openActionSettings();

    /**
     * 开关"输入模式"：全局监听键盘，敲键时触发「输入」动作。
     * 这是挂钩式能力，**默认关闭**（settings_.data.inputMode 初始为 false）。
     */
    void setInputMode(bool on);
    bool inputMode() const { return settings_.data.inputMode; }

    /** 动态效果强度：0=关 1=弱 2=中 3=强（呼吸 / 打字回弹 / 拖拽倾斜） */
    void setMotionLevel(int level);
    int motionLevel() const { return settings_.data.motionLevel; }

    /** 加载单个素材文件（该文件即待机动作）。失败返回 false 并把原因写进 lastError() */
    bool loadMedia(const QString& path);
    QString lastError() const { return lastError_; }
    bool hasMedia() const { return actions_.isValid(); }

    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAnimationTick();
    void onWatchTick();
    void onPerfTick();
    void onTopmostTick();
    void onDragStarted();
    void onDragEnded();
    void onClicked();
    void onDoubleClicked();
    /** 输入模式：收到全局按键（vk 是 Windows 虚拟键码，down=按下） */
    void onKeyEvent(quint32 vk, bool down);
    /** 动态通道的每帧推进 */
    void onMotionTick();
    void onContextMenu(const QPoint& globalPos);
    void onFileDropped(const QString& path);
    void onWheelZoomed(int steps);

private:
    void buildTray();
    void refreshRegions();
    /** 切到某个动作：换序列、按 loop 决定播完是否回待机。
     *  force=true 时即使已经在放同一个循环动作也重新从头播。 */
    void playAction(PetAction a, bool force = false);
    /** 距离"下一帧"还有多少毫秒（给播放定时器用） */
    int nextFrameDelayMs() const;
    /** 由「基础素材文件（= 待机）+ 设置里逐项指定的动作素材」组合出动作集。
     *  基础素材挂了但指定项齐备时仍算成功。失败时把原因写进 *error。 */
    bool composeActions(const QString& base, ActionSet* out, QString* error) const;
    /** 用 composeActions(settings.lastMedia) 重建动作集，并立即生效 */
    void reloadActions();
    /** 该动作配的音频文件（空 = 不发声）。音频与素材完全独立。 */
    QString audioFor(PetAction a) const;
    /** 按设置强度填动态通道的参数 */
    void applyMotionParams();
    /** 把设置窗口里的控件与真实状态对齐（窗口只发意图、不写设置，所以每次都要回灌） */
    void refreshSettingsWindow();
    /** 外观页改动：落盘 + 立即应用 */
    void applyAppearance(int scalePercent, int opacityPercent, bool alwaysOnTop, bool paused);
    /** 弹素材选择框（右键菜单与设置窗口共用同一段逻辑） */
    void chooseMediaFromDialog();
    /** 起动态定时器（静止后会自动停） */
    void startMotion();
    void updateWindowSize();
    /** 当前是否有可播放的动画（无素材时的程序化圆盘也算） */
    bool animatable() const;
    /** 窗口像素尺寸。**固定按最大缩放（kMaxScalePercent）预留**——缩放时永不 resize，
     *  这是"完全不闪"的前提。无素材时返回空 QSize。 */
    QSize spriteWindowSize() const;
    /** 把当前缩放应用到渲染端（uScale），并按"精灵实际绘制区域"重算可交互区 */
    void applyZoomToRenderer();
    /** 在「有动画 && 未暂停 && 未隐藏」的前提下启动动画定时器；已在跑则什么都不做。
     *  启动前会重置 animClock_，避免恢复播放时 playhead_ 跳一大步。 */
    void resumeAnimation();
    /** 按百分比缩放，并保持窗口中心不动（视觉上比以左上角为锚点自然得多） */
    void setScalePercent(int percent);
    /** 暂停 / 继续动画。暂停时停掉动画定时器（同时省 CPU），画面停在当前帧。 */
    void setPaused(bool paused);
    /** 缩放回到 100% 并移回主屏右下角默认位置 */
    void resetPlacement();
    void rebuildMenu(QMenu* menu);

    PetWindow* window_ = nullptr;
    IClickThrough* clickThrough_ = nullptr;
    HitRegions regions_;
    QRect cachedRect_;

    QTimer* animTimer_ = nullptr;
    QTimer* watchTimer_ = nullptr;
    QTimer* perfTimer_ = nullptr;
    QTimer* topmostTimer_ = nullptr;
    QTimer* zoomSettleTimer_ = nullptr;   // 缩放停顿判定：期间冻结穿透翻转、结束时才写盘
    QTimer* actionHoldTimer_ = nullptr;   // 静态的一次性动作：停一下再回待机
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    // 动作设置窗口：无父对象 + WA_DeleteOnClose，关闭即自删；靠 destroyed() 信号置空
    ActionSettingsDialog* actionDialog_ = nullptr;
    KeyboardMonitor* keyboard_ = nullptr;   // 输入模式用；不启用时根本不创建
    SettingsWindow* settingsWindow_ = nullptr;   // 设置窗口，首次打开时创建，之后复用
    qint64 lastInputTickMs_ = 0;            // 打字节流的上一次时刻
    int ticksThisSecond_ = 0;               // 播放定时器实际唤醒次数（诊断用）

    // ---- 动态通道（呼吸 / 打字回弹 / 拖拽倾斜）----
    PetMotion motion_;
    QTimer* motionTimer_ = nullptr;
    QElapsedTimer motionClock_;
    int lastDragX_ = 0;                     // 拖拽倾斜用：上一帧的窗口 x
    KeyBubble* keyBubble_ = nullptr;        // 按键气泡；输入模式开启时才创建

    QElapsedTimer animClock_;
    mutable PerfProbe probe_;

    FrameSequence seq_;             // 当前正在播放的动作序列（= actions_ 里当前动作的 seq）
    ActionSet actions_;             // 待机 / 单击 / 双击 / 拖拽
    PetAction currentAction_ = PetAction::Idle;
    double idlePlayhead_ = 0.0;     // 离开待机时的播放头，回待机时接着放（避免"跳回第一帧"）
    bool loopCurrent_ = true;       // 当前动作是否循环
    int holdMsCurrent_ = kDefaultHoldMs;   // 当前动作（静态素材）的停留时长
    double playhead_ = 0.0;        // 播放头，始终落在 [0, totalDuration)
    int currentFrame_ = -1;

    Settings settings_;
    int targetSize_ = 360;
    bool suspended_ = false;       // 隐藏时挂起定时器
    bool paused_ = false;          // 用户手动暂停（不持久化，每次启动都重新播放）
    QString lastError_;
    QString loadNote_;

    double phase_ = 0.0;
    int framesThisSecond_ = 0;
    qint64 lastRenderedFrames_ = 0;
    bool dragging_ = false;
    bool menuOpen_ = false;
    bool zooming_ = false;         // 缩放进行中：暂停穿透翻转，避免窗口样式反复切换造成闪动
    QPoint lastCursor_;            // 光标去重：没动就不重复做命中判定
    QRect lastGeo_;
};

} // namespace pic2pet
