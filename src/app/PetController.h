#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>

#include "ClickThrough.h"
#include "FrameSequence.h"
#include "HitRegions.h"
#include "PerfProbe.h"
#include "Settings.h"

class QMenu;
class QSystemTrayIcon;
class QTimer;

namespace pic2pet {

class PetWindow;

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

    /**
     * 启动。cliMedia 非空则加载它，否则回退到上次记住的素材。
     * 两者只走一条路径，避免素材被加载两次。
     */
    void start(const QString& cliMedia = QString());
    void switchBackend(ClickThroughBackend backend);
    QString statusLine() const;

    /** 抓取当前渲染结果（离屏，不依赖窗口是否可见），用于自动化验证渲染路径 */
    bool grabFramebuffer(const QString& path);

    /** 加载素材（文件或目录）。失败返回 false 并把原因写进 lastError() */
    bool loadMedia(const QString& path);
    QString lastError() const { return lastError_; }
    bool hasMedia() const { return seq_.isValid(); }

    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAnimationTick();
    void onWatchTick();
    void onPerfTick();
    void onTopmostTick();
    void onDragStarted();
    void onDragEnded();
    void onContextMenu(const QPoint& globalPos);
    void onFileDropped(const QString& path);
    void onWheelZoomed(int steps);

private:
    void buildTray();
    void refreshRegions();
    void applySequence();
    void updateWindowSize();
    /** 按百分比缩放，并保持窗口中心不动（视觉上比以左上角为锚点自然得多） */
    void setScalePercent(int percent);
    void rebuildMenu(QMenu* menu);

    PetWindow* window_ = nullptr;
    IClickThrough* clickThrough_ = nullptr;
    HitRegions regions_;
    QRect cachedRect_;

    QTimer* animTimer_ = nullptr;
    QTimer* watchTimer_ = nullptr;
    QTimer* perfTimer_ = nullptr;
    QTimer* topmostTimer_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;

    QElapsedTimer animClock_;
    mutable PerfProbe probe_;

    FrameSequence seq_;
    double playhead_ = 0.0;        // 播放头，始终落在 [0, totalDuration)
    int currentFrame_ = -1;

    Settings settings_;
    int targetSize_ = 360;
    bool suspended_ = false;       // 隐藏时挂起定时器
    QString lastError_;
    QString loadNote_;

    double phase_ = 0.0;
    int framesThisSecond_ = 0;
    qint64 lastRenderedFrames_ = 0;
    bool dragging_ = false;
    bool menuOpen_ = false;
    QPoint lastCursor_;            // 光标去重：没动就不重复做命中判定
    QRect lastGeo_;
};

} // namespace pic2pet
