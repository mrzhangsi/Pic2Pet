#include "PetController.h"

#include "ActionSettingsDialog.h"
#include "AudioPlayer.h"
#include "AutoStart.h"
#include "PetRenderer.h"
#include "PetWindow.h"

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

constexpr int kAnimationIntervalMs = 16;   // 播放驱动：保持 16ms，不按素材帧率降频（降频会抖）
constexpr int kWatchIntervalMs = 32;       // 穿透调度，30Hz 足够；再快只是白烧 CPU
constexpr int kPerfIntervalMs = 1000;
constexpr int kTopmostIntervalMs = 2000;

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

    window_->setWindowOpacity(settings_.data.opacity);
    window_->resize(size, size);

    if (useTray) buildTray();
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

void PetController::openActionSettings() {
    if (!actionDialog_) {
        // 无父窗口：它是普通窗口，不能跟桌宠的"透明/置顶/无边框"缠在一起。
        // WA_DeleteOnClose + QPointer：关掉即释放，不长期占内存。
        auto* dlg = new ActionSettingsDialog(&settings_);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &ActionSettingsDialog::actionsChanged, this, &PetController::reloadActions);
        connect(dlg, &ActionSettingsDialog::previewRequested, this,
                [this](PetAction a) { playAction(a, true); });
        connect(dlg, &QObject::destroyed, this, [this] { actionDialog_ = nullptr; });
        actionDialog_ = dlg;
    }
    actionDialog_->show();
    actionDialog_->raise();
    actionDialog_->activateWindow();
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

    currentAction_ = a;
    loopCurrent_ = c.loop;
    holdMsCurrent_ = c.holdMs;
    seq_ = c.seq;

    currentFrame_ = -1;
    playhead_ = 0.0;

    window_->renderer()->setSprite(seq_);
    updateWindowSize();   // 窗口尺寸只由"待机动作"的宽高比决定，所以换动作不会 resize

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
        return false;
    }
    if (t == QEvent::Show) {
        suspended_ = false;
        watchTimer_->start(kWatchIntervalMs);
        resumeAnimation();
        return false;
    }
    if (t == QEvent::WindowStateChange) {
        const bool minimized = window_ && window_->isMinimized();
        if (minimized) {
            suspended_ = true;
            animTimer_->stop();
            watchTimer_->stop();
        } else if (suspended_ && window_ && window_->isVisible()) {
            suspended_ = false;
            watchTimer_->start(kWatchIntervalMs);
            resumeAnimation();
        }
        return false;
    }
    return QObject::eventFilter(watched, event);
}

void PetController::onAnimationTick() {
    if (!window_ || !window_->renderer()) return;

    if (!window_->renderer()->hasSprite()) {
        // 没有素材时退回 M0 的程序化动画
        const double dt = animClock_.restart() / 1000.0;
        phase_ += dt * 1.2;
        if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
        window_->renderer()->setPhase(phase_);
        window_->renderer()->update();
        ++framesThisSecond_;
        return;
    }

    if (seq_.isStatic()) return;   // 静态图不该走到这里

    const double dt = animClock_.restart() / 1000.0;
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
    if (index == currentFrame_) return;      // ② 帧没变：什么都不做
    currentFrame_ = index;

    window_->renderer()->showFrame(index);
    ++framesThisSecond_;
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
               "[perf] timerFps=%1 renderFps=%2 cpu=%3%% mem=%4 toggles=%5 backend=%6 sprite=%7")
               .arg(framesThisSecond_)
               .arg(renderedDelta)
               .arg(sample.cpuPercent, 0, 'f', 1)
               .arg(PerfProbe::formatBytes(sample.memoryBytes))
               .arg(clickThrough_ ? clickThrough_->toggleCount() : 0)
               .arg(clickThrough_ ? clickThrough_->name() : QStringLiteral("none"))
               .arg(sprite);
    framesThisSecond_ = 0;
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
    playAction(PetAction::Drag);        // 没提供拖拽动作时，内部会自动忽略
}

void PetController::onDragEnded() {
    dragging_ = false;
    if (window_) {
        settings_.data.pos = window_->pos();
        settings_.save();
    }
    playAction(PetAction::Idle, true);  // 松手回待机
}

void PetController::onClicked() {
    playAction(PetAction::Click);
}

void PetController::onDoubleClicked() {
    playAction(PetAction::DoubleClick);
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

    menu->addAction(QStringLiteral("打开素材…"), this, [this] {
        if (!window_) return;
        menuOpen_ = true;
        const QString path = QFileDialog::getOpenFileName(
            window_, QStringLiteral("选择桌宠素材"), QString(),
            QStringLiteral("图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
        menuOpen_ = false;
        if (!path.isEmpty()) loadMedia(path);
    });

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
                                   PetAction::DoubleClick, PetAction::Drag };
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

    menu->addAction(QStringLiteral("动作设置…"), this, [this] { openActionSettings(); });

    if (AutoStart::supported()) {
        auto* starter = menu->addAction(QStringLiteral("开机自启"));
        starter->setCheckable(true);
        starter->setChecked(AutoStart::isEnabled());
        connect(starter, &QAction::toggled, this, [](bool on) {
            const bool ok = AutoStart::setEnabled(on);
            if (!ok) qWarning() << "[pet] 开机自启设置失败";
        });
    }

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
