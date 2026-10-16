#include "PetController.h"

#include "AudioPlayer.h"
#include "AutoStart.h"
#include "KeyBubble.h"
#include "KeyboardMonitor.h"
#include "PetRenderer.h"
#include "PetWindow.h"
#include "SettingsWindow.h"
#include "TeacherStats.h"
#include "Theme.h"
#include "WebFilter.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindowStateChangeEvent>

#include <algorithm>

#ifdef Q_OS_WIN
// windows.h 里的 min/max 宏会破坏 std::min / std::max，必须先禁掉
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace pic2pet {

namespace {

// 播放驱动改成"按下一帧的 deadline 唤醒"（学 BongoCat 的 deadline 等待）：
// 100ms 一帧的素材就 100ms 醒一次，而不是 16ms 空转 6 次。
// 下限 16ms = 60fps 上限：高于 60fps 的素材到点只呈现一次，多出来的帧跳过（时钟照走、不累积延迟）。
constexpr int kAnimationIntervalMs = 16;
constexpr int kAnimationMaxIntervalMs = 250;    // 上限：素材很慢时也要周期性醒来
constexpr double kMaxFrameDeltaSec = 0.25;      // 卡顿/休眠后单帧最多推进这么多，避免画面跳一大段

constexpr int kWatchIntervalMs = 32;       // 穿透调度，30Hz 足够；再快只是白烧 CPU
// 动态通道（呼吸/回弹/倾斜）的推进频率。30Hz 对弹簧已经够顺滑，
// 比 60Hz 少一半的 DWM 合成——透明分层窗口的合成才是大头，不是我们的绘制。
constexpr int kMotionIntervalMs = 33;
constexpr int kPerfIntervalMs = 1000;
constexpr int kTopmostIntervalMs = 2000;
/** 输入模式下两次"敲键"之间的最小间隔：打字很快时不要每一下都放动画 */
constexpr int kInputCooldownMs = 150;

// 应用图标来自 app.qrc（src/app/app.ico），窗口图标与托盘图标共用同一份。
// 若资源缺失（例如有人只拷了 exe），退回一个简单的蓝色圆点，保证托盘不空着。
QIcon appIcon() {
    const QIcon ic(QStringLiteral(":/app.ico"));
    if (!ic.availableSizes().isEmpty()) return ic;

    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4C, 0x9A, 0xFF));
    p.drawEllipse(4, 4, 24, 24);
    return QIcon(pm);
}

} // namespace

PetController::PetController(ClickThroughBackend backend, int size, bool useTray, QObject* parent)
    : QObject(parent), targetSize_(size) {
    settings_.load();

    window_ = new PetWindow(size, nullptr);
    window_->installEventFilter(this);
    connect(window_, &PetWindow::dragStarted, this, &PetController::onDragStarted);
    connect(window_, &PetWindow::dragEnded, this, &PetController::onDragEnded);
    connect(window_, &PetWindow::clicked, this, &PetController::onClicked);
    connect(window_, &PetWindow::doubleClicked, this, &PetController::onDoubleClicked);
    connect(window_, &PetWindow::contextMenuRequested, this, &PetController::onContextMenu);
    connect(window_, &PetWindow::fileDropped, this, &PetController::onFileDropped);
    connect(window_, &PetWindow::wheelZoomed, this, &PetController::onWheelZoomed);

    clickThrough_ = createClickThrough(backend, window_);

    animTimer_ = new QTimer(this);
    animTimer_->setTimerType(Qt::PreciseTimer);
    connect(animTimer_, &QTimer::timeout, this, &PetController::onAnimationTick);

    watchTimer_ = new QTimer(this);
    watchTimer_->setTimerType(Qt::PreciseTimer);
    connect(watchTimer_, &QTimer::timeout, this, &PetController::onWatchTick);

    perfTimer_ = new QTimer(this);
    connect(perfTimer_, &QTimer::timeout, this, &PetController::onPerfTick);

    topmostTimer_ = new QTimer(this);
    connect(topmostTimer_, &QTimer::timeout, this, &PetController::onTopmostTick);

    // 缩放"停顿"判定：连滚期间保持 zooming_，期间冻结穿透翻转；停下来才写盘。
    // 这样每个滚动 notch 都不用做 QSettings::sync()，也不会反复切换窗口样式。
    zoomSettleTimer_ = new QTimer(this);
    zoomSettleTimer_->setSingleShot(true);
    connect(zoomSettleTimer_, &QTimer::timeout, this, [this] {
        zooming_ = false;
        settings_.save();
    });

    // 静态的一次性动作（例如"点击"只给了一张 PNG）没有动画可播完，
    // 用它停留一会儿再自动回待机。
    actionHoldTimer_ = new QTimer(this);
    actionHoldTimer_->setSingleShot(true);
    connect(actionHoldTimer_, &QTimer::timeout, this, [this] { playAction(PetAction::Idle, true); });

    motionTimer_ = new QTimer(this);
    connect(motionTimer_, &QTimer::timeout, this, &PetController::onMotionTick);

    window_->setWindowOpacity(settings_.data.opacity);
    window_->resize(size, size);

    if (useTray) buildTray();
}

PetController::~PetController() {
    delete keyBubble_;      // 顶层小窗口没有 QObject 父对象，得自己收
    if (teacher_) {
        // 进程要退了：先礼貌停止（最多等一个 HTTP 超时），等不起再强杀
        teacher_->stop();
        if (!teacher_->wait(5000)) {
            teacher_->terminate();
            teacher_->wait(1000);
        }
        delete teacher_;
    }
    // 用户要求「关闭 Pic2Pet 时限制不保留」：退出即解除 Edge 限制。
    // 没开启过时 clear() 见到没有所有权标记会直接返回，绝不碰用户自己的策略值
    clearWebFilter();
}

void PetController::start(const QString& cliMedia) {
    if (settings_.data.pos.x() >= 0) {
        window_->move(settings_.data.pos);
    } else {
        QScreen* screen = QApplication::primaryScreen();
        const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
        window_->move(avail.right() - window_->width() - 60,
                      avail.bottom() - window_->height() - 60);
    }

    refreshRegions();
    window_->show();

    // 只加载一次：命令行优先，其次恢复上次的素材
    const QString toLoad = cliMedia.isEmpty() ? settings_.data.lastMedia : cliMedia;
    if (!toLoad.isEmpty()) loadMedia(toLoad);

    if (!seq_.isValid()) {
        // 没有素材就回到 M0 的程序化圆盘
        resumeAnimation();
    }

    watchTimer_->start(kWatchIntervalMs);
    perfTimer_->start(kPerfIntervalMs);
    topmostTimer_->start(kTopmostIntervalMs);

    // 输入模式：上次开着的话这里恢复。默认 false，所以正常情况下不会装钩子
    if (settings_.data.inputMode) setInputMode(true);

    // 网站白名单：上次开着的话重放一次。两条路都会走到这——
    //   ① 正常退出已清除，这里按 enabled 重新写上（状态一致）
    //   ② 进程被异常杀死（任务管理器等），策略可能还挂着：apply() 幂等，
    //      发现注册表里已是目标状态就什么都不做
    //
    // **刻意不在这里弹 UAC**：开机就问一次管理员密码没人受得了。
    // 策略还挂着（情况②）就直接是"限制生效中"，不需要重写；
    // 真被清掉了（情况①）也等用户下次自己开开关时再写 —— 那时弹 UAC 才是合理的。
    // 注意这里**不碰注册表**：读 HKLM 策略键不需要提权。
    if (settings_.data.teacher.webFilter.enabled) {
        const bool stillActive = WebFilter::isActive();
        qInfo().noquote()
            << QStringLiteral("[webfilter] 上次是开启的，注册表状态：%1")
                   .arg(stillActive ? QStringLiteral("策略仍在（限制生效中）")
                                    : QStringLiteral("已被清除（需手动重新开启）"));
        if (settingsWindow_) refreshSettingsWindow();
    }

    // 动态通道：按上次的强度起步（强度 0 时什么都不会起）
    applyMotionParams();
    startMotion();
}

void PetController::switchBackend(ClickThroughBackend backend) {
    const bool current = clickThrough_ ? clickThrough_->currentValue() : false;
    delete clickThrough_;
    clickThrough_ = createClickThrough(backend, window_);
    clickThrough_->setTransparent(current);
    qInfo().noquote() << "[pet] click-through backend ->" << clickThrough_->name();
}

bool PetController::grabFramebuffer(const QString& path) {
    if (!window_ || !window_->renderer()) return false;
    // grabFramebuffer() 会强制在当前 GL 上下文里渲染一帧再读回像素，
    // 因此即使窗口被全屏应用盖住也能验证渲染路径
    const QImage img = window_->renderer()->grabFramebuffer();
    if (img.isNull()) {
        qWarning() << "[pet] grabFramebuffer 返回空图像";
        return false;
    }
    return img.save(path, "PNG");
}

bool PetController::composeActions(const QString& base, ActionSet* out, QString* error) const {
    ActionSet set;
    QString baseErr;

    if (!base.isEmpty()) {
        if (!set.load(base, &baseErr)) {
            // 基础路径挂了不代表没救：也许"动作设置"里逐项都指定了
            set = ActionSet();
        } else {
            baseErr.clear();
        }
    }

    struct Override {
        PetAction action;
        const ActionConfig* cfg;
    };
    const Override kOverrides[] = {
        { PetAction::Idle,        &settings_.data.actionIdle },
        { PetAction::Click,       &settings_.data.actionClick },
        { PetAction::DoubleClick, &settings_.data.actionDoubleClick },
        { PetAction::Drag,        &settings_.data.actionDrag },
        { PetAction::Typing,      &settings_.data.actionTyping },
    };
    for (const Override& o : kOverrides) {
        if (o.cfg->media.isEmpty()) {
            // 没另指定素材（比如待机就沿用 lastMedia），但停留时长仍可能被改过
            set.setHoldMs(o.action, o.cfg->holdMs);
            continue;
        }
        QString err;
        if (!set.setActionFromPath(o.action, o.cfg->media, o.cfg->holdMs, &err)) {
            qWarning().noquote() << "[pet] 指定动作加载失败：" << err;
        }
    }

    set.ensureIdle();   // 覆盖之后才可能出现"有素材但没待机"的情况

    if (!set.isValid()) {
        if (error) *error = baseErr.isEmpty() ? QStringLiteral("没有可播放的动作") : baseErr;
        return false;
    }
    if (error) error->clear();
    *out = set;
    return true;
}

bool PetController::loadMedia(const QString& path) {
    ActionSet set;
    QString err;
    if (!composeActions(path, &set, &err)) {
        lastError_ = err;
        qWarning().noquote() << "[pet] 加载失败：" << err;
        return false;
    }

    // 记住这份素材文件："动作设置"里的指定项就是在这个基础上逐项覆盖
    settings_.rememberMedia(path);
    settings_.save();

    actions_ = set;
    lastError_.clear();
    loadNote_.clear();
    paused_ = false;   // 主动换素材视为"我要看它"，自动取消暂停，避免加载后停在空白帧

    playAction(PetAction::Idle, true);

    const FrameSequence& idle = actions_.idle().seq;
    if (tray_) {
        tray_->setToolTip(idle.sourceName.isEmpty()
                              ? QStringLiteral("Pic2Pet")
                              : QStringLiteral("Pic2Pet · %1").arg(idle.sourceName));
    }

    qInfo().noquote()
        << QStringLiteral("[pet] 已加载 %1：待机 %2x%3 · 帧内存合计 %4 MB · 动作 [%5]")
               .arg(idle.sourceName.isEmpty() ? QStringLiteral("<memory>") : idle.sourceName)
               .arg(idle.width)
               .arg(idle.height)
               .arg(double(actions_.bytes()) / (1024.0 * 1024.0), 0, 'f', 1)
               .arg(actions_.summary());
    return true;
}

void PetController::reloadActions() {
    ActionSet set;
    QString err;
    if (!composeActions(settings_.data.lastMedia, &set, &err)) {
        qWarning().noquote() << "[pet] 动作重载失败，保留原有动作集：" << err;
        return;
    }

    actions_ = set;
    playAction(PetAction::Idle, true);   // 从待机重来，避免停在已失效的动作上
    qInfo().noquote() << QStringLiteral("[pet] 动作已更新 [%1]").arg(actions_.summary());
}

ActionConfig& PetController::actionConfig(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return settings_.data.actionIdle;
    case PetAction::Click:       return settings_.data.actionClick;
    case PetAction::DoubleClick: return settings_.data.actionDoubleClick;
    case PetAction::Drag:        return settings_.data.actionDrag;
    case PetAction::Typing:      return settings_.data.actionTyping;
    }
    return settings_.data.actionIdle;   // 不可达
}

void PetController::openActionSettings() {
    // 动作设置已经并进新版设置窗口的「动作」页（原独立窗口已删除）
    openSettings(1);
}

void PetController::playAction(PetAction a, bool force) {
    if (!window_ || !window_->renderer()) return;

    // 该动作没提供 → 不打断当前动作（待机继续）
    if (a != PetAction::Idle && !actions_.has(a)) return;

    const ActionClip& c = actions_.clip(a);
    if (!c.isValid()) return;

    // 已经在放同一个循环动作：没必要重来
    if (!force && a == currentAction_ && c.loop) return;

    if (actionHoldTimer_) actionHoldTimer_->stop();

    // 离开待机时记下播放头：回到待机要**接着放**，而不是从第一帧重来。
    // （否则单击/双击/拖拽一结束，待机动画就会"啪"地跳回开头，很跳戏。）
    if (currentAction_ == PetAction::Idle && seq_.isValid()) idlePlayhead_ = playhead_;

    currentAction_ = a;
    loopCurrent_ = c.loop;
    holdMsCurrent_ = c.holdMs;
    seq_ = c.seq;

    currentFrame_ = -1;
    if (a == PetAction::Idle && idlePlayhead_ > 0.0 && seq_.totalDuration > 0.0) {
        playhead_ = std::fmod(idlePlayhead_, seq_.totalDuration);
    } else {
        playhead_ = 0.0;
    }

    window_->renderer()->setSprite(seq_);
    updateWindowSize();      // 窗口尺寸只由"待机动作"的宽高比决定，所以换动作不会 resize
    applyZoomToRenderer();   // 但精灵矩形会变（宽高比/缩放都可能不同）→ 刷新命中区与气泡锚点

    if (seq_.isStatic()) {
        // ① 静态素材：上传一次就够了，之后不再有任何绘制
        window_->renderer()->showFrame(0);
        animTimer_->stop();
        if (!loopCurrent_) {
            // 静态的一次性动作没有"播完"这回事：按配置的停留时长停一下，再回待机
            actionHoldTimer_->start(holdMsCurrent_);
        }
    } else {
        resumeAnimation();
    }

    // 音频与素材完全独立：只要这个动作配了音频就播（会打断上一次）
    const QString audio = audioFor(a);
    if (!audio.isEmpty()) audioPlayer().play(audio);

    qInfo().noquote() << QStringLiteral("[pet] 动作 -> %1 %2%3")
                             .arg(ActionSet::actionName(a),
                                  loopCurrent_ ? QStringLiteral("(循环)")
                                               : QStringLiteral("(一次)"))
                             .arg(audio.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" · 音频 %1")
                                            .arg(QFileInfo(audio).fileName()));
}

QString PetController::audioFor(PetAction a) const {
    switch (a) {
    case PetAction::Idle:        return settings_.data.actionIdle.audio;
    case PetAction::Click:       return settings_.data.actionClick.audio;
    case PetAction::DoubleClick: return settings_.data.actionDoubleClick.audio;
    case PetAction::Drag:        return settings_.data.actionDrag.audio;
    case PetAction::Typing:      return settings_.data.actionTyping.audio;
    }
    return QString();
}

QSize PetController::spriteWindowSize() const {
    // 窗口尺寸固定由**待机动作**的宽高比决定（不是当前动作）：
    // 这样切换动作时窗口不会 resize，也就不会闪。
    // 其它动作宽高比不同没关系——渲染端用双轴缩放等比铺进来。
    if (!actions_.isValid()) return QSize();

    const FrameSequence& idle = actions_.idle().seq;
    const double base = double(std::max(idle.width, idle.height));
    if (base <= 0) return QSize();

    // 固定按**最大**缩放预留：缩放过程中窗口尺寸永不变化 —— 这是"完全不闪"的前提。
    // （Windows 上只要改窗口尺寸，透明 + OpenGL 窗口的合成表面就要重建，必然闪一帧。）
    const double scale = (double(targetSize_) / base) * (kMaxScalePercent / 100.0);

    return QSize(std::max(16, int(idle.width * scale + 0.5)),
                 std::max(16, int(idle.height * scale + 0.5)));
}

void PetController::updateWindowSize() {
    if (!window_) return;
    const QSize s = spriteWindowSize();
    if (s.isEmpty()) return;
    window_->resize(s);
    applyZoomToRenderer();
}

void PetController::applyZoomToRenderer() {
    if (!window_ || !window_->renderer()) return;
    if (!seq_.isValid()) return;

    const QSize win = window_->size();
    if (win.width() <= 0 || win.height() <= 0) return;

    // 先让当前动作"等比铺满"窗口（窗口是按待机动作的宽高比预留的），再叠上缩放。
    // 双轴分开算，所以宽高比不同的动作也不会被拉变形。
    const double fit = std::min(double(win.width()) / seq_.width,
                                double(win.height()) / seq_.height);
    const double total = fit * (double(settings_.data.scalePercent) / kMaxScalePercent);

    window_->renderer()->setSpriteScale(float(double(seq_.width) * total / win.width()),
                                       float(double(seq_.height) * total / win.height()));

    // 精灵在窗口里的实际绘制区域（居中）→ 可交互区跟着它走，
    // 否则窗口比精灵大出来的透明区域会吃掉鼠标事件
    const int dw = std::max(1, int(double(seq_.width) * total + 0.5));
    const int dh = std::max(1, int(double(seq_.height) * total + 0.5));
    window_->setSpriteRect(QRect((win.width() - dw) / 2, (win.height() - dh) / 2, dw, dh));

    refreshRegions();
}

void PetController::setScalePercent(int percent) {
    if (!window_) return;

    const int clamped = std::max(kMinScalePercent, std::min(kMaxScalePercent, percent));
    if (clamped == settings_.data.scalePercent) return;

    settings_.data.scalePercent = clamped;

    // 窗口已按最大缩放固定预留 → 这里**完全不碰窗口**，所以一次都不会闪。
    // 缩放的全部视觉变化由渲染端 uScale 完成。
    if (!seq_.isValid()) {          // 无素材：不涉及渲染，只记下数值
        settings_.save();
        return;
    }

    applyZoomToRenderer();
    settings_.data.pos = window_->pos();

    // 缩放让精灵尺寸变化 → 可交互区跟着变；期间冻结穿透翻转，避免来回切窗口样式，
    // 停 250ms 后再统一收尾（含写盘）。
    zooming_ = true;
    zoomSettleTimer_->start(250);
    lastCursor_ = QPoint(-1, -1);   // 缩放结束后强制重新判定一次穿透
}

bool PetController::animatable() const {
    // 无素材时跑的是 M0 的程序化圆盘，也算"有动画"；静态图则没有可暂停的东西
    return !seq_.isValid() || !seq_.isStatic();
}

void PetController::resumeAnimation() {
    if (!animTimer_ || !animatable() || paused_ || suspended_) return;
    if (animTimer_->isActive()) return;         // 已经在跑就别重置时钟
    animClock_.start();                         // 先归零，避免第一帧 dt 过大导致 playhead_ 跳帧
    animTimer_->start(kAnimationIntervalMs);
}

void PetController::setPaused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;

    if (paused_) {
        // 停掉动画定时器：画面停在当前帧，CPU 也一并降下来
        if (animTimer_) animTimer_->stop();
    } else {
        resumeAnimation();
    }

    qInfo().noquote() << QStringLiteral("[pet] %1")
                             .arg(paused_ ? QStringLiteral("已暂停动画") : QStringLiteral("继续播放"));
}

void PetController::resetPlacement() {
    if (!window_) return;

    // 缩放回到 100%。窗口尺寸固定不变，所以这里只移动、不 resize —— 也就不会闪。
    settings_.data.scalePercent = 100;
    if (seq_.isValid()) applyZoomToRenderer();

    QScreen* screen = QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    window_->move(avail.center() - QPoint(window_->width() / 2, window_->height() / 2));

    settings_.data.pos = window_->pos();
    settings_.save();

    qInfo().noquote() << QStringLiteral("[pet] 已重置到屏幕中心 -> %1x%2 @ (%3,%4)")
                             .arg(window_->width())
                             .arg(window_->height())
                             .arg(window_->x())
                             .arg(window_->y());
}

void PetController::refreshRegions() {
    if (!window_) return;
    const QRect rect = window_->interactiveRect();
    if (rect == cachedRect_) return;
    cachedRect_ = rect;
    regions_.clear();
    regions_.add(rect);
}

bool PetController::eventFilter(QObject* watched, QEvent* event) {
    if (watched != window_) return QObject::eventFilter(watched, event);

    const QEvent::Type t = event->type();
    if (t == QEvent::Hide) {
        // ③ 窗口不可见：挂起所有周期性工作
        suspended_ = true;
        animTimer_->stop();
        watchTimer_->stop();
        motionTimer_->stop();
        return false;
    }
    if (t == QEvent::Show) {
        suspended_ = false;
        watchTimer_->start(kWatchIntervalMs);
        resumeAnimation();
        startMotion();
        return false;
    }
    if (t == QEvent::WindowStateChange) {
        const bool minimized = window_ && window_->isMinimized();
        if (minimized) {
            suspended_ = true;
            animTimer_->stop();
            watchTimer_->stop();
            motionTimer_->stop();
        } else if (suspended_ && window_ && window_->isVisible()) {
            suspended_ = false;
            watchTimer_->start(kWatchIntervalMs);
            resumeAnimation();
            startMotion();
        }
        return false;
    }
    return QObject::eventFilter(watched, event);
}

int PetController::nextFrameDelayMs() const {
    if (!seq_.isValid() || seq_.isStatic() || seq_.totalDuration <= 0.0) {
        return kAnimationIntervalMs;
    }

    // 下一帧的起始时刻（最后一帧之后就是循环回开头）
    const int idx = seq_.frameIndexAt(playhead_);
    const double next = (idx + 1 < seq_.count()) ? seq_.starts.at(idx + 1) : seq_.totalDuration;

    double remaining = next - playhead_;
    if (remaining < 0.0) remaining = 0.0;

    // 向下取整再多给 1ms：Qt/Win32 的定时器可能略早到，不留余量就会多醒一次才发现"帧还没变"
    const int ms = int(remaining * 1000.0) + 1;
    return std::clamp(ms, kAnimationIntervalMs, kAnimationMaxIntervalMs);
}

void PetController::onAnimationTick() {
    if (!window_ || !window_->renderer()) return;
    ++ticksThisSecond_;   // 只诊断用：定时器实际醒了几次（与"换了几帧"区分开）

    if (!window_->renderer()->hasSprite()) {
        // 没有素材时退回 M0 的程序化动画
        double dt = animClock_.restart() / 1000.0;
        if (dt > kMaxFrameDeltaSec) dt = kMaxFrameDeltaSec;
        phase_ += dt * 1.2;
        if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
        window_->renderer()->setPhase(phase_);
        window_->renderer()->update();
        ++framesThisSecond_;
        return;
    }

    if (seq_.isStatic()) return;   // 静态图不该走到这里

    double dt = animClock_.restart() / 1000.0;
    // 卡顿/休眠后 dt 可能是好几秒，截断掉，否则一醒来画面直接跳一大段
    if (dt > kMaxFrameDeltaSec) dt = kMaxFrameDeltaSec;
    if (dt < 0.0) dt = 0.0;

    playhead_ += dt;
    if (playhead_ >= seq_.totalDuration) {
        if (!loopCurrent_) {
            playAction(PetAction::Idle, true);   // 一次性动作播完 → 回待机
            return;
        }
        // 手工取模而不是 fmod：帧数少时更快，也不会累积浮点误差
        playhead_ -= seq_.totalDuration;
        if (playhead_ < 0.0) playhead_ = 0.0;
    }

    const int index = seq_.frameIndexAt(playhead_);
    if (index != currentFrame_) {
        currentFrame_ = index;
        window_->renderer()->showFrame(index);
        ++framesThisSecond_;
    }

    // ④ 按"下一帧什么时候到"重新排定时（BongoCat 的 deadline 等待）。
    //    帧没变时也重排一次，免得早到后还按旧间隔睡过头。
    const int nextMs = nextFrameDelayMs();
    if (nextMs != animTimer_->interval()) animTimer_->start(nextMs);
}

void PetController::onWatchTick() {
    if (!window_ || !clickThrough_) return;

    // 拖拽中 / 菜单打开 / 缩放进行中：保证能收到鼠标，且不要反复切窗口样式（切换本身会闪）
    if (dragging_ || menuOpen_ || zooming_) {
        clickThrough_->setTransparent(false);
        // 失效去重缓存：等状态恢复后必须重新判定一次，否则会一直保持"不穿透"
        lastCursor_ = QPoint(-1, -1);
        return;
    }

    const QRect geo = window_->geometry();
    const QPoint cursor = QCursor::pos();

    // 光标没动、窗口也没动过，判定结果必然和上次一样——直接跳过，这是待机 CPU≈0 的关键
    if (cursor == lastCursor_ && geo == lastGeo_) return;
    lastCursor_ = cursor;
    lastGeo_ = geo;

    const bool inWindow = geo.contains(cursor);

    refreshRegions();
    const bool hit = inWindow && regions_.hit(cursor - geo.topLeft());

    clickThrough_->setTransparent(!hit);
}

void PetController::onPerfTick() {
    const PerfSample sample = probe_.sample();

    qint64 rendered = 0;
    if (window_ && window_->renderer()) rendered = window_->renderer()->frameCount();
    const qint64 renderedDelta = rendered - lastRenderedFrames_;
    lastRenderedFrames_ = rendered;

    QString sprite = QStringLiteral("none");
    if (seq_.isValid()) {
        sprite = QStringLiteral("%1x%2/%3f")
                     .arg(seq_.width)
                     .arg(seq_.height)
                     .arg(seq_.count());
    }

    qInfo().noquote()
        << QStringLiteral(
               "[perf] wakeFps=%1 frameFps=%2 renderFps=%3 cpu=%4%% mem=%5 toggles=%6"
               " backend=%7 sprite=%8")
               .arg(ticksThisSecond_)
               .arg(framesThisSecond_)
               .arg(renderedDelta)
               .arg(sample.cpuPercent, 0, 'f', 1)
               .arg(PerfProbe::formatBytes(sample.memoryBytes))
               .arg(clickThrough_ ? clickThrough_->toggleCount() : 0)
               .arg(clickThrough_ ? clickThrough_->name() : QStringLiteral("none"))
               .arg(sprite);
    framesThisSecond_ = 0;
    ticksThisSecond_ = 0;
}

void PetController::onTopmostTick() {
#ifdef Q_OS_WIN
    if (!window_) return;
    if (!settings_.data.alwaysOnTop) return;
    HWND hwnd = reinterpret_cast<HWND>(window_->winId());
    if (!hwnd) return;
    const LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOPMOST) == 0) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

void PetController::onDragStarted() {
    dragging_ = true;
    if (window_) lastDragX_ = window_->x();
    playAction(PetAction::Drag);        // 没提供拖拽动作时，内部会自动忽略
    startMotion();                      // 拖拽倾斜要靠动态通道跑起来
}

void PetController::onDragEnded() {
    dragging_ = false;
    if (window_) {
        settings_.data.pos = window_->pos();
        settings_.save();
    }
    motion_.setLeanTarget(0.0);         // 松手回正
    startMotion();
    // **不要 force**：没配拖拽动作时上面那个 playAction(Drag) 直接返回了、当前仍是待机，
    // 这时强制重进待机会把播放头清零 —— 表现就是"一松手动画跳回第一帧"。
    // 不带 force 时，"已经在放待机"会被 playAction 里的短路挡住，动画就接着放。
    playAction(PetAction::Idle);
}

void PetController::onClicked() {
    playAction(PetAction::Click);
}

void PetController::onDoubleClicked() {
    playAction(PetAction::DoubleClick);
}

void PetController::chooseMediaFromDialog() {
    if (!window_) return;
    menuOpen_ = true;      // 对话框期间保持"能收鼠标"，并且不要反复切窗口样式
    const QString path = QFileDialog::getOpenFileName(
        window_, QStringLiteral("选择桌宠素材"), QString(),
        QStringLiteral("图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
    menuOpen_ = false;
    if (!path.isEmpty()) {
        loadMedia(path);
        refreshSettingsWindow();
    }
}

void PetController::refreshSettingsWindow() {
    if (!settingsWindow_) return;
    settingsWindow_->refresh();
    settingsWindow_->setAutoStartState(AutoStart::supported(), AutoStart::isEnabled());
    settingsWindow_->setPausedState(paused_);
    settingsWindow_->setTeacherRunning(teacher_ != nullptr);
    if (teacher_) settingsWindow_->setTeacherStatus(lastTeacherStatus_);
    settingsWindow_->setWebFilterError(webFilterError_);
    settingsWindow_->setWebFilterBusy(webFilterBusy_);   // refresh() 会重置开关 enabled，必须补回
    settingsWindow_->setWebFilterPolicyActive(WebFilter::isActive());
}

void PetController::applyAppearance(int scalePercent, int opacityPercent, bool alwaysOnTop,
                                    bool paused) {
    setScalePercent(scalePercent);

    const double opacity = opacityPercent / 100.0;
    if (!qFuzzyCompare(settings_.data.opacity, opacity)) {
        settings_.data.opacity = opacity;
        if (window_) window_->setWindowOpacity(opacity);
    }

    if (settings_.data.alwaysOnTop != alwaysOnTop) {
        settings_.data.alwaysOnTop = alwaysOnTop;
        if (window_) {
            window_->setWindowFlag(Qt::WindowStaysOnTopHint, alwaysOnTop);
            window_->show();
        }
    }

    setPaused(paused);
    settings_.save();
}

void PetController::openSettings(int page) {
    if (!settingsWindow_) {
        settingsWindow_ = new SettingsWindow(&settings_, nullptr);

        // 窗口只发意图，落盘与生效都在这里做 —— 做完回灌一次，保证 UI 与真实状态一致
        connect(settingsWindow_, &SettingsWindow::inputModeChanged, this,
                [this](bool on) { setInputMode(on); refreshSettingsWindow(); });
        connect(settingsWindow_, &SettingsWindow::keyBubbleChanged, this, [this](bool on) {
            settings_.data.keyBubble = on;
            settings_.save();
            if (!on && keyBubble_) keyBubble_->hide();
        });
        connect(settingsWindow_, &SettingsWindow::motionLevelChanged, this,
                [this](int level) { setMotionLevel(level); });
        connect(settingsWindow_, &SettingsWindow::chooseMediaRequested, this,
                [this] { chooseMediaFromDialog(); });
        connect(settingsWindow_, &SettingsWindow::recentMediaChosen, this,
                [this](const QString& path) {
                    if (loadMedia(path)) reloadActions();
                    refreshSettingsWindow();
                });
        // ---- 动作页 ----
        connect(settingsWindow_, &SettingsWindow::actionMediaChosen, this,
                [this](PetAction a, const QString& path) {
                    actionConfig(a).media = path;
                    settings_.save();
                    reloadActions();
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::actionAudioChosen, this,
                [this](PetAction a, const QString& path) {
                    actionConfig(a).audio = path;
                    settings_.save();
                    reloadActions();
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::actionHoldChanged, this,
                [this](PetAction a, int holdMs) {
                    actionConfig(a).holdMs = holdMs;
                    settings_.save();
                    reloadActions();
                });
        connect(settingsWindow_, &SettingsWindow::actionCleared, this, [this](PetAction a) {
            ActionConfig& cfg = actionConfig(a);
            if (cfg.media.isEmpty() && cfg.audio.isEmpty()) return;
            cfg.media.clear();
            cfg.audio.clear();
            settings_.save();
            reloadActions();
            refreshSettingsWindow();
        });
        connect(settingsWindow_, &SettingsWindow::actionsResetRequested, this, [this] {
            for (PetAction a : { PetAction::Idle, PetAction::Click, PetAction::DoubleClick,
                                 PetAction::Drag, PetAction::Typing }) {
                actionConfig(a).media.clear();
                actionConfig(a).audio.clear();
            }
            settings_.save();
            reloadActions();
            refreshSettingsWindow();
        });
        connect(settingsWindow_, &SettingsWindow::previewActionRequested, this,
                [this](PetAction a) { playAction(a, true); });
        connect(settingsWindow_, &SettingsWindow::appearanceChanged, this,
                [this](int scale, int opacity, bool onTop, bool paused) {
                    applyAppearance(scale, opacity, onTop, paused);
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::autoStartChanged, this, [this](bool on) {
            if (AutoStart::supported()) AutoStart::setEnabled(on);
            refreshSettingsWindow();
        });
        connect(settingsWindow_, &SettingsWindow::hidePetRequested, this, [this] {
            if (window_) window_->hide();
        });
        connect(settingsWindow_, &SettingsWindow::resetPlacementRequested, this, [this] {
            resetPlacement();
            refreshSettingsWindow();
        });
        // ---- AI 教官页：只发意图，落盘 / 启停都在这里做 ----
        connect(settingsWindow_, &SettingsWindow::teacherGoalEdited, this,
                [this](const QString& goal) {
                    if (goal.isEmpty()) return;
                    settings_.data.teacher.goal = goal;
                    settings_.save();
                    if (teacher_) teacher_->setGoal(goal);   // 运行中即时生效
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::teacherProviderEdited, this,
                [this](const QString& baseUrl, const QString& model, const QString& key) {
                    if (!baseUrl.isEmpty()) settings_.data.teacher.api.baseUrl = baseUrl;
                    if (!model.isEmpty()) settings_.data.teacher.api.model = model;
                    if (!key.isEmpty()) settings_.data.teacher.api.apiKey = key;
                    settings_.save();
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::teacherCaptureChanged, this,
                [this](bool monitorCamera, bool uploadCamera) {
                    settings_.data.teacher.monitorCamera = monitorCamera;
                    settings_.data.teacher.uploadCamera = uploadCamera;
                    settings_.save();
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::teacherPomodoroChanged, this,
                [this](bool enabled, int focusMinutes, int breakMinutes) {
                    settings_.data.teacher.pomodoro.enabled = enabled;
                    settings_.data.teacher.pomodoro.focusMinutes = focusMinutes;
                    settings_.data.teacher.pomodoro.breakMinutes = breakMinutes;
                    settings_.save();
                    refreshSettingsWindow();
                });
        connect(settingsWindow_, &SettingsWindow::teacherStartRequested, this,
                [this] { startTeacher(); });
        connect(settingsWindow_, &SettingsWindow::teacherStopRequested, this,
                [this] { stopTeacher(); });
        // ---- 网站白名单（Edge）：同样只发意图 ----
        connect(settingsWindow_, &SettingsWindow::teacherWebFilterToggled, this,
                [this](bool on) { setWebFilter(on, settings_.data.teacher.webFilter.entries); });
        connect(settingsWindow_, &SettingsWindow::teacherWebFilterEntriesChanged, this,
                [this](const QStringList& entries) {
                    settings_.data.teacher.webFilter.entries = entries;
                    settings_.save();
                    // 开着的时候改条目 = 立即重写策略（Edge 动态刷新，不用重启浏览器）
                    if (settings_.data.teacher.webFilter.enabled) applyWebFilter();
                    if (settingsWindow_) {
                        settingsWindow_->setWebFilterError(webFilterError_);
                        settingsWindow_->refresh();
                    }
                });
    }

    refreshSettingsWindow();
    settingsWindow_->showPage(page);
    settingsWindow_->show();
    settingsWindow_->raise();
    settingsWindow_->activateWindow();
}

void PetController::setInputMode(bool on) {
    settings_.data.inputMode = on;

    if (on) {
        if (!keyboard_) {
            keyboard_ = new KeyboardMonitor(this);
            connect(keyboard_, &KeyboardMonitor::keyEvent, this, &PetController::onKeyEvent);
        }
        // 顶层小窗口（不能挂在桌宠窗口上：那是 OpenGL 窗口，子控件容易和它打架）
        if (!keyBubble_) keyBubble_ = new KeyBubble(nullptr);

        QString err;
        if (!keyboard_->start(&err)) {
            // 装不上就老实关掉，别留一个"看起来开着其实没生效"的开关
            qWarning().noquote() << "[pet] 输入模式启动失败：" << err;
            settings_.data.inputMode = false;
            settings_.save();
            return;
        }
        qInfo().noquote() << "[pet] 输入模式已开启（" << keyboard_->name() << "）";
    } else {
        if (keyboard_) keyboard_->stop();
        if (keyBubble_) keyBubble_->hide();      // 关了输入模式，气泡也不该再冒出来
        qInfo().noquote() << "[pet] 输入模式已关闭";
    }

    settings_.save();
}

// ---------------------------------------------------------------- AI 教官

namespace {

/** 告警提示音：三声短促的 880Hz（对齐参考实现的 winsound.Beep） */
void teacherAlertBeep() {
#ifdef Q_OS_WIN
    Beep(880, 180);
    Beep(880, 180);
    Beep(880, 180);
#else
    QApplication::beep();
#endif
}

/** 弹托盘通知。没有托盘就退回日志（通知发不出去不该影响主流程） */
void teacherNotify(QSystemTrayIcon* tray, const QString& title, const QString& message) {
    if (tray) {
        tray->showMessage(title, message, QSystemTrayIcon::Warning, 8000);
    } else {
        qInfo().noquote() << QStringLiteral("[teacher][通知] %1 | %2").arg(title, message);
    }
}

} // namespace

void PetController::startTeacher() {
    if (teacher_) {
        // 上一个还在收尾（停止要等线程退出）。这时候别再起一个，
        // 否则会有两个监控同时截屏 / 开摄像头。
        qInfo().noquote() << QStringLiteral("[teacher] 正在停止中，稍候再启动");
        return;
    }

    teacher_ = new TeacherMonitor(settings_.data.teacher, nullptr);
    // 监控跑在自己的线程；这些连接自动成为 queued，槽函数仍在主线程执行
    connect(teacher_, &TeacherMonitor::started, this, &PetController::onTeacherStarted);
    connect(teacher_, &TeacherMonitor::statusChanged, this, &PetController::onTeacherStatus);
    connect(teacher_, &TeacherMonitor::alert, this, &PetController::onTeacherAlert);
    connect(teacher_, &TeacherMonitor::pomodoroBreak, this, &PetController::onTeacherBreak);
    connect(teacher_, &TeacherMonitor::pomodoroFocus, this, &PetController::onTeacherFocus);
    connect(teacher_, &TeacherMonitor::finished, this, &PetController::onTeacherFinished);

    teacher_->start();
    qInfo().noquote() << QStringLiteral("[teacher] 启动：目标=%1").arg(settings_.data.teacher.goal);
}

void PetController::stopTeacher() {
    if (!teacher_) return;
    teacher_->stop();
    // 界面立刻反映"已停止"，线程在后台收尾（最多一个 HTTP 超时），
    // finished 信号到达时才真正删除对象。
    if (settingsWindow_) settingsWindow_->setTeacherRunning(false);
    if (tray_) tray_->setToolTip(QStringLiteral("Pic2Pet"));
}

void PetController::setWebFilter(bool on, const QStringList& entries) {
    // 空名单禁止开启：URLBlocklist 是 "*"（全拦），空 allowlist 会把 Edge 整个关死。
    // UI 层有同样的约束，这里再兜一层（右键菜单 / 配置被手改都可能绕开 UI）
    if (on && entries.isEmpty()) {
        webFilterError_ = QStringLiteral("白名单还是空的：先添加至少一个网站再开启");
        qWarning().noquote() << QStringLiteral("[webfilter] 拒绝开启：白名单为空");
        if (settingsWindow_) {
            settingsWindow_->setWebFilterError(webFilterError_);
            settingsWindow_->refresh();
        }
        return;
    }

    settings_.data.teacher.webFilter.enabled = on;
    settings_.data.teacher.webFilter.entries = entries;
    settings_.save();

    if (on) {
        applyWebFilter();
    } else {
        clearWebFilter();
    }

    if (settingsWindow_) {
        settingsWindow_->setWebFilterError(webFilterError_);
        settingsWindow_->refresh();
    }
}

void PetController::applyWebFilter() {
    if (!WebFilter::isSupported()) {
        // 非 Windows：记住开关但不生效（其他平台没有 Edge 策略这东西）
        qInfo().noquote() << QStringLiteral("[webfilter] 当前平台不支持，仅 Windows 生效");
        return;
    }
    if (webFilterBusy_) {
        qInfo().noquote() << QStringLiteral("[webfilter] 上一次操作还在进行，忽略重复请求");
        return;
    }

    const QStringList entries = settings_.data.teacher.webFilter.entries;
    webFilterBusy_ = true;
    if (settingsWindow_) {
        settingsWindow_->setWebFilterBusy(true);
        settingsWindow_->setWebFilterError(QString());
    }

    // 提权要弹 UAC，可能十几秒 —— 必须异步，绝不卡住 GUI
    WebFilterElevated::applyAsync(entries, [this, entries](bool ok, const QString& error) {
        webFilterBusy_ = false;
        if (ok) {
            webFilterError_.clear();
            qInfo().noquote()
                << QStringLiteral("[webfilter] 已开启：Edge 仅允许 %1 个白名单网站"
                                  "（机器级；Edge 需重启或等约 15 分钟生效）")
                       .arg(entries.size());
        } else {
            // 失败：把开关拨回去，绝不留「设置说开着、实际没拦」的两套状态
            webFilterError_ = error;
            settings_.data.teacher.webFilter.enabled = false;
            settings_.save();
            qWarning().noquote() << QStringLiteral("[webfilter] 开启失败：%1").arg(error);
            teacherNotify(tray_, QStringLiteral("网站白名单开启失败"), error);
        }
        if (settingsWindow_) {
            settingsWindow_->setWebFilterBusy(false);
            settingsWindow_->setWebFilterError(webFilterError_);
            settingsWindow_->refresh();
            settingsWindow_->setWebFilterPolicyActive(WebFilter::isActive());
        }
    });
}

void PetController::clearWebFilter() {
    if (!WebFilter::isSupported()) return;
    if (webFilterBusy_) return;

    // 同步等待：这条路径只在"关闭白名单"与"退出 Pic2Pet"时走。
    // 退出时已经没有事件循环了，必须等子进程真的把策略清掉再让主进程结束，
    // 否则会留下一个"软件没了但 Edge 还在拦"的半截状态。
    webFilterBusy_ = true;
    QString error;
    const bool ok = WebFilterElevated::clearBlocking(&error);
    webFilterBusy_ = false;

    if (ok) {
        webFilterError_.clear();
        qInfo().noquote() << QStringLiteral("[webfilter] 已清除，Edge 恢复自由访问");
    } else {
        webFilterError_ = error;
        qWarning().noquote() << QStringLiteral("[webfilter] 清除失败：%1").arg(error);
    }
    if (settingsWindow_) {
        settingsWindow_->setWebFilterBusy(false);
        settingsWindow_->setWebFilterError(webFilterError_);
        settingsWindow_->refresh();
        settingsWindow_->setWebFilterPolicyActive(WebFilter::isActive());
    }
}

void PetController::onTeacherStarted(const QString& goal, bool hasApiKey, bool pomodoro) {
    if (settingsWindow_) settingsWindow_->setTeacherRunning(true);
    if (tray_) {
        tray_->setToolTip(QStringLiteral("Pic2Pet · AI 教官：%1").arg(goal));
    }
    qInfo().noquote() << QStringLiteral("[teacher] 已启动 | 目标=%1 | api=%2 | 课堂模式=%3")
                             .arg(goal, hasApiKey ? QStringLiteral("有") : QStringLiteral("无"),
                                  pomodoro ? QStringLiteral("开") : QStringLiteral("关"));
}

void PetController::onTeacherStatus(const TeacherStatus& status) {
    lastTeacherStatus_ = status;
    if (settingsWindow_) settingsWindow_->setTeacherStatus(status);
}

void PetController::onTeacherAlert(const QString& reason, const QString& goal) {
    teacherNotify(tray_, QStringLiteral("⚠️ 疑似离开学习"),
                  QStringLiteral("已连续未在学习：%1\n目标：%2").arg(reason, goal));
    teacherAlertBeep();
}

void PetController::onTeacherBreak(const QString& message) {
    teacherNotify(tray_, QStringLiteral("该休息一下了"), message);
    teacherAlertBeep();
}

void PetController::onTeacherFocus(const QString& message) {
    teacherNotify(tray_, QStringLiteral("休息结束"), message);
    teacherAlertBeep();
}

void PetController::onTeacherFinished(const TeacherReport& report) {
    // 完整报告进日志（不弹窗：桌宠不该在用户桌上乱弹东西）
    qInfo().noquote() << QStringLiteral("[teacher] ── 会话报告 · %1 ──").arg(report.goal);
    for (const auto& row : teacherFormatReport(report.stats)) {
        if (row.second.isEmpty()) {
            qInfo().noquote() << row.first;
        } else {
            qInfo().noquote() << QStringLiteral("  %1  %2").arg(row.first, row.second);
        }
    }

    lastTeacherStatus_ = TeacherStatus{};
    if (teacher_) {
        teacher_->deleteLater();
        teacher_ = nullptr;
    }
    if (settingsWindow_) settingsWindow_->setTeacherRunning(false);
    if (tray_) tray_->setToolTip(QStringLiteral("Pic2Pet"));
}

void PetController::onKeyEvent(quint32 vk, bool down) {
    if (!down) return;                            // 只在按下时触发

    // 按键气泡：**每个键都要显示**，所以放在节流之前（节流只该管动作和回弹）。
    // 锚点用**精灵的实际矩形**：窗口是按最大缩放预留的，精灵只占其中一块、还会随缩放变。
    if (keyBubble_ && settings_.data.keyBubble && window_) {
        const QRect spriteLocal = window_->interactiveRect();
        const QRect spriteGlobal(spriteLocal.topLeft() + window_->mapToGlobal(QPoint(0, 0)),
                                 spriteLocal.size());
        keyBubble_->showKey(vk);
        keyBubble_->placeNearSprite(spriteGlobal);
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastInputTickMs_ < kInputCooldownMs) return;   // 节流
    lastInputTickMs_ = now;

    playAction(PetAction::Typing);

    // 动态通道：给弹簧一个冲量（"一弹一弹"）。曾经还按键位左右加点横向/倾斜，
    // 实测幅度调大也不明显，已去掉 —— 现在"按了哪个键"由按键气泡来交代。
    motion_.impulse();
    startMotion();
}

void PetController::applyMotionParams() {
    PetMotion::Params p;
    const int level = settings_.data.motionLevel;
    p.enabled = (level > 0);

    switch (level) {
    case 0:                                     // 关
        p.breathAmp = 0.0;
        p.bounceAmp = 0.0;
        p.leanDeg = 0.0;
        break;
    case 1:                                     // 弱：只有回弹，不呼吸（静止时能真正停定时器）
        p.breathAmp = 0.0;
        p.bounceAmp = 0.05;
        p.leanDeg = 6.0;
        break;
    case 3:                                     // 强
        p.breathAmp = 0.025;
        p.bounceAmp = 0.12;
        p.leanDeg = 14.0;
        break;
    default:                                    // 中：单次按键压扁 8%，拖拽最多歪 10°
        p.breathAmp = 0.015;
        p.bounceAmp = 0.08;
        p.leanDeg = 10.0;
        break;
    }
    motion_.setParams(p);
}

void PetController::startMotion() {
    if (settings_.data.motionLevel <= 0) return;
    if (!window_ || !window_->renderer()) return;
    if (motionTimer_->isActive()) return;
    motionClock_.start();
    motionTimer_->start(kMotionIntervalMs);
}

void PetController::onMotionTick() {
    if (!window_ || !window_->renderer()) return;

    // 拖拽倾斜：用"这一帧窗口移动了多少像素"当速度，**朝运动反方向倒**（惯性：像拎着玩偶往右甩，
    // 下半身跟着手走、上半身在后面拖）。
    // 符号约定：rotateDeg > 0 = 逆时针 = 顶端往**左**，所以往右拖(vel>0)要用正值。
    if (dragging_) {
        const int x = window_->x();
        const double v = double(x - lastDragX_);
        lastDragX_ = x;
        const double maxLean = motion_.params().leanDeg;
        motion_.setLeanTarget(std::clamp(v * 0.7, -maxLean, maxLean));
    }

    const double dt = motionClock_.restart() / 1000.0;
    if (!motion_.step(dt)) {
        // 已经静止：归位、停表，别再白烧 CPU
        motionTimer_->stop();
        window_->renderer()->setTransform(PetTransform());
        return;
    }
    window_->renderer()->setTransform(motion_.transform());
}

void PetController::setMotionLevel(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    settings_.data.motionLevel = level;
    settings_.save();

    applyMotionParams();
    if (level <= 0) {
        motionTimer_->stop();
        if (window_ && window_->renderer()) window_->renderer()->setTransform(PetTransform());
        qInfo().noquote() << "[pet] 动态效果已关闭";
    } else {
        startMotion();
        qInfo().noquote() << QStringLiteral("[pet] 动态效果 -> %1").arg(level);
    }
}

void PetController::onFileDropped(const QString& path) {
    qInfo().noquote() << "[pet] 拖入：" << path;
    loadMedia(path);
}

void PetController::onWheelZoomed(int steps) {
    // 每格 10 个百分点，缩放时保持窗口中心不动
    setScalePercent(settings_.data.scalePercent + steps * 10);
    qInfo().noquote() << QStringLiteral("[pet] 缩放 -> %1%").arg(settings_.data.scalePercent);
}

void PetController::rebuildMenu(QMenu* menu) {
    menu->clear();

    menu->addAction(QStringLiteral("设置…"), this, [this] { openSettings(); });

    menu->addAction(QStringLiteral("打开素材…"), this, [this] { chooseMediaFromDialog(); });

    if (!settings_.data.recentMedia.isEmpty()) {
        auto* recent = menu->addMenu(QStringLiteral("最近素材"));
        for (const QString& p : settings_.data.recentMedia) {
            recent->addAction(QFileInfo(p).fileName(), this, [this, p] { loadMedia(p); });
        }
    }

    menu->addSeparator();

    auto* scaleMenu = menu->addMenu(QStringLiteral("缩放"));
    auto* scaleGroup = new QActionGroup(scaleMenu);
    scaleGroup->setExclusive(true);
    for (int pct : { 50, 75, 100, 150, 200 }) {
        auto* a = scaleMenu->addAction(QString::number(pct) + QLatin1Char('%'));
        a->setCheckable(true);
        a->setChecked(settings_.data.scalePercent == pct);
        scaleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, pct] { setScalePercent(pct); });
    }

    auto* opacityMenu = menu->addMenu(QStringLiteral("透明度"));
    auto* opacityGroup = new QActionGroup(opacityMenu);
    opacityGroup->setExclusive(true);
    for (int pct : { 100, 80, 60, 40 }) {
        auto* a = opacityMenu->addAction(QString::number(pct) + QLatin1Char('%'));
        a->setCheckable(true);
        a->setChecked(qAbs(settings_.data.opacity * 100 - pct) < 1.0);
        opacityGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, pct] {
            settings_.data.opacity = pct / 100.0;
            if (window_) window_->setWindowOpacity(settings_.data.opacity);
            settings_.save();
        });
    }

    menu->addSeparator();

    // 暂停 / 继续：静态图没有可暂停的东西，直接禁用；暂停会同时停掉定时器省 CPU
    auto* pauseAction = menu->addAction(paused_ ? QStringLiteral("继续动画")
                                                : QStringLiteral("暂停动画"));
    pauseAction->setEnabled(animatable());
    connect(pauseAction, &QAction::triggered, this, [this] { setPaused(!paused_); });

    menu->addAction(QStringLiteral("重置位置与大小"), this, [this] { resetPlacement(); });

    // 动作子菜单：既能手动触发（方便验证），也能一眼看出哪些动作已提供
    if (actions_.isValid()) {
        auto* actionMenu = menu->addMenu(QStringLiteral("动作"));
        const PetAction kAll[] = { PetAction::Idle, PetAction::Click,
                                   PetAction::DoubleClick, PetAction::Drag,
                                   PetAction::Typing };
        for (PetAction a : kAll) {
            const bool ok = actions_.has(a);
            auto* act = actionMenu->addAction(
                ok ? ActionSet::actionName(a)
                   : QStringLiteral("%1（未提供）").arg(ActionSet::actionName(a)));
            act->setEnabled(ok);
            connect(act, &QAction::triggered, this, [this, a] { playAction(a, true); });
        }

        auto* info = menu->addAction(QStringLiteral("素材：%1 · %2% · 待机 %3x%4")
                                         .arg(actions_.idle().seq.sourceName)
                                         .arg(settings_.data.scalePercent)
                                         .arg(actions_.idle().seq.width)
                                         .arg(actions_.idle().seq.height));
        info->setEnabled(false);   // 纯展示，不可点
    }

    menu->addSeparator();

    menu->addAction(QStringLiteral("动作设置…"), this, [this] { openSettings(1); });

    // 动态效果：呼吸 / 打字回弹 / 拖拽倾斜。
    // 注意"中/强"会开着呼吸 = 一直在动，静态素材的 CPU 会从 ~0 变成持续重绘。
    auto* motionMenu = menu->addMenu(QStringLiteral("动态效果"));
    motionMenu->setToolTip(QStringLiteral("呼吸 / 打字回弹 / 拖拽倾斜；"
                                          "「中」「强」含呼吸，会持续重绘"));
    auto* motionGroup = new QActionGroup(motionMenu);
    motionGroup->setExclusive(true);
    static const char* const kLevelNames[4] = { "关", "弱", "中", "强" };
    for (int level = 0; level <= 3; ++level) {
        auto* act = motionMenu->addAction(QString::fromUtf8(kLevelNames[level]));
        act->setCheckable(true);
        act->setChecked(settings_.data.motionLevel == level);
        connect(act, &QAction::triggered, this, [this, level] { setMotionLevel(level); });
        motionGroup->addAction(act);
    }

    // 输入模式：全局键盘监听。默认关闭，勾选是唯一开关
    auto* inputAction = menu->addAction(QStringLiteral("输入模式（跟随键盘）"));
    inputAction->setCheckable(true);
    inputAction->setChecked(settings_.data.inputMode);
    inputAction->setToolTip(QStringLiteral("开启后全局监听键盘（只取虚拟键码，不记录按键内容），"
                                           "你敲键时桌宠播放「输入」动作。会加重杀软误报。"));
    connect(inputAction, &QAction::toggled, this, [this](bool on) { setInputMode(on); });

    // 按键气泡：输入模式下敲键时，在桌宠上方显示按下的键
    auto* bubbleAction = menu->addAction(QStringLiteral("按键提示（气泡）"));
    bubbleAction->setCheckable(true);
    bubbleAction->setChecked(settings_.data.keyBubble);
    bubbleAction->setToolTip(QStringLiteral("敲键时在桌宠上方冒个气泡显示刚按下的键。"
                                            "只在屏幕上短暂显示，不记录、不落盘。"));
    connect(bubbleAction, &QAction::toggled, this, [this](bool on) {
        settings_.data.keyBubble = on;
        settings_.save();
        if (!on && keyBubble_) keyBubble_->hide();
    });

    if (AutoStart::supported()) {
        auto* starter = menu->addAction(QStringLiteral("开机自启"));
        starter->setCheckable(true);
        starter->setChecked(AutoStart::isEnabled());
        connect(starter, &QAction::toggled, this, [](bool on) {
            const bool ok = AutoStart::setEnabled(on);
            if (!ok) qWarning() << "[pet] 开机自启设置失败";
        });
    }

    // AI 教官：右键一键启动（默认配置 = 目标「学习」）；运行中再点一次 = 停止
    auto* teacherAction = menu->addAction(QStringLiteral("AI 教官（学习）"));
    teacherAction->setCheckable(true);
    teacherAction->setChecked(teacher_ != nullptr);
    teacherAction->setToolTip(QStringLiteral(
        "定期看你在做什么，分心了弹托盘通知 + 提示音。\n"
        "启动用当前保存的配置（默认：目标「学习」+ DeepSeek，间隔 60~300s 自适应）。\n"
        "只在启动后定时截屏 / 拍照，图片不落盘；停止后完全静默。"));
    connect(teacherAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            startTeacher();
        } else {
            stopTeacher();
        }
    });
    menu->addAction(QStringLiteral("AI 教官设置…"), this, [this] { openSettings(6); });

    // 网站白名单（Edge）：独立开关，与 AI 教官运行状态无关。
    // 勾选态直接读设置值（apply 失败时会被拨回 false，不会说谎）
    auto* webAction = menu->addAction(QStringLiteral("网站白名单（Edge）"));
    webAction->setCheckable(true);
    webAction->setChecked(settings_.data.teacher.webFilter.enabled);
    webAction->setToolTip(QStringLiteral(
        "开启后本机 Edge 只能访问白名单里的网站（路径按前缀匹配）。\n"
        "用的是 Edge 官方策略，无需管理员权限；Pic2Pet 退出时自动解除。\n"
        "白名单条目在「AI 教官设置…」页里配置。"));
    // 注意：setChecked 在 connect 之前——QAction 程序化改状态也会发 toggled，
    // 那时候还没有接收者，不会误触发（与上面 AI 教官项同一套路）
    connect(webAction, &QAction::toggled, this, [this](bool on) {
        setWebFilter(on, settings_.data.teacher.webFilter.entries);
        // 开启失败（空名单 / 组织策略冲突等）：托盘是右键路径上唯一的反馈渠道
        if (on && !settings_.data.teacher.webFilter.enabled) {
            teacherNotify(tray_, QStringLiteral("网站白名单开启失败"), webFilterError_);
        }
    });

    auto* topAction = menu->addAction(QStringLiteral("始终置顶"));
    topAction->setCheckable(true);
    topAction->setChecked(settings_.data.alwaysOnTop);
    connect(topAction, &QAction::toggled, this, [this](bool on) {
        settings_.data.alwaysOnTop = on;
        if (!window_) return;
        window_->setWindowFlag(Qt::WindowStaysOnTopHint, on);
        window_->show();
        settings_.save();
    });

    menu->addSeparator();
    menu->addAction(QStringLiteral("显示 / 隐藏"), this, [this] {
        if (window_) window_->setVisible(!window_->isVisible());
    });
    menu->addAction(QStringLiteral("打印状态"), this, [this] { qInfo().noquote() << statusLine(); });

    menu->addSeparator();
    menu->addAction(QStringLiteral("退出"), qApp, &QCoreApplication::quit);

    // 菜单是顶层窗口，不会从设置窗口继承样式，得单独套一份
    menu->setStyleSheet(theme::menuStyleSheet());
}

void PetController::onContextMenu(const QPoint& globalPos) {
    QMenu menu;
    rebuildMenu(&menu);
    menuOpen_ = true;
    menu.exec(globalPos);
    menuOpen_ = false;
}

void PetController::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "[pet] 系统托盘不可用，跳过托盘图标";
        return;
    }
    tray_ = new QSystemTrayIcon(appIcon(), this);
    tray_->setToolTip(QStringLiteral("Pic2Pet"));

    trayMenu_ = new QMenu();
    tray_->setContextMenu(trayMenu_);
    connect(trayMenu_, &QMenu::aboutToShow, this, [this] { rebuildMenu(trayMenu_); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::DoubleClick && window_) {
            window_->setVisible(!window_->isVisible());
        }
    });
    rebuildMenu(trayMenu_);
    tray_->show();
}

QString PetController::statusLine() const {
    QString gl = QStringLiteral("n/a");
    if (window_ && window_->renderer()) gl = window_->renderer()->glInfo();

    const QScreen* screen = QApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    return QStringLiteral(
               "[status] Qt=%1 screens=%2 dpr=%3 gl=%4 backend=%5 hit=%6 media=%7 frames=%8")
        .arg(QStringLiteral(QT_VERSION_STR))
        .arg(QApplication::screens().size())
        .arg(dpr, 0, 'f', 2)
        .arg(gl)
        .arg(clickThrough_ ? clickThrough_->name() : QStringLiteral("none"))
        .arg(cachedRect_.isValid() ? QStringLiteral("%1x%2")
                                         .arg(cachedRect_.width())
                                         .arg(cachedRect_.height())
                                   : QStringLiteral("none"))
        .arg(seq_.isValid() ? QStringLiteral("%1x%2").arg(seq_.width).arg(seq_.height)
                            : QStringLiteral("none"))
        .arg(seq_.isValid() ? seq_.count() : 0);
}

} // namespace pic2pet
