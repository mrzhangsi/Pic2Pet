#include "PetController.h"

#include "AutoStart.h"
#include "MediaLoader.h"
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

QIcon makeTrayIcon() {
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
        animClock_.start();
        animTimer_->start(kAnimationIntervalMs);
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

bool PetController::loadMedia(const QString& path) {
    const MediaResult r = MediaLoader::loadPath(path);
    if (!r.ok) {
        lastError_ = r.error;
        qWarning().noquote() << "[pet] 加载失败：" << r.error;
        return false;
    }

    seq_ = r.seq;
    lastError_.clear();
    loadNote_.clear();

    settings_.rememberMedia(path);
    settings_.save();

    applySequence();

    qInfo().noquote()
        << QStringLiteral("[pet] 已加载 %1：%2x%3 · %4 帧 · %5 · 帧内存 %6 MB · 总时长 %7 ms")
               .arg(seq_.sourceName.isEmpty() ? QStringLiteral("<memory>") : seq_.sourceName)
               .arg(seq_.width)
               .arg(seq_.height)
               .arg(seq_.count())
               .arg(seq_.isStatic() ? QStringLiteral("静态") : QStringLiteral("动画"))
               .arg(double(seq_.bytes()) / (1024.0 * 1024.0), 0, 'f', 1)
               .arg(qint64(seq_.totalDuration * 1000.0));
    return true;
}

void PetController::applySequence() {
    if (!window_ || !window_->renderer()) return;

    currentFrame_ = -1;
    playhead_ = 0.0;

    if (!seq_.isValid()) {
        window_->renderer()->clearSprite();
        if (!suspended_) animTimer_->start(kAnimationIntervalMs);
        return;
    }

    window_->renderer()->setSprite(seq_);
    updateWindowSize();

    if (seq_.isStatic()) {
        // ① 静态图：上传一次就够了，之后不再有任何绘制
        window_->renderer()->showFrame(0);
        animTimer_->stop();
    } else {
        animClock_.start();
        if (!suspended_) animTimer_->start(kAnimationIntervalMs);
    }
}

void PetController::updateWindowSize() {
    if (!window_ || !seq_.isValid()) return;

    const double base = double(std::max(seq_.width, seq_.height));
    if (base <= 0) return;
    const double scale = (double(targetSize_) / base) * (settings_.data.scalePercent / 100.0);

    const int w = std::max(16, int(seq_.width * scale + 0.5));
    const int h = std::max(16, int(seq_.height * scale + 0.5));
    window_->resize(w, h);
    refreshRegions();
}

void PetController::setScalePercent(int percent) {
    if (!window_) return;

    const int clamped = std::max(25, std::min(400, percent));
    if (clamped == settings_.data.scalePercent) return;

    // 以窗口中心为锚点：直接 resize 的话会往右下角长，手感很怪
    const QPoint center = window_->geometry().center();

    settings_.data.scalePercent = clamped;
    updateWindowSize();

    const QRect geo = window_->geometry();
    window_->move(center - QPoint(geo.width() / 2, geo.height() / 2));

    settings_.data.pos = window_->pos();
    settings_.save();
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
        if (!seq_.isStatic()) animTimer_->start(kAnimationIntervalMs);
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
            if (!seq_.isStatic()) animTimer_->start(kAnimationIntervalMs);
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

    // 拖拽中 / 菜单打开时必须保证能收到鼠标
    if (dragging_ || menuOpen_) {
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

void PetController::onDragStarted() { dragging_ = true; }

void PetController::onDragEnded() {
    dragging_ = false;
    if (!window_) return;
    settings_.data.pos = window_->pos();
    settings_.save();
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
            QStringLiteral("图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp *.webp);;所有文件 (*.*)"));
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
    for (int pct : { 50, 75, 100, 150, 200, 300 }) {
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

    if (seq_.isValid() && !seq_.sourceName.isEmpty()) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("素材：%1").arg(seq_.sourceName));
    }

    menu->addSeparator();

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
    tray_ = new QSystemTrayIcon(makeTrayIcon(), this);
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
