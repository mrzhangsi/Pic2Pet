#include "SettingsWindow.h"

#include "SegmentedControl.h"
#include "SidebarItem.h"
#include "Theme.h"
#include "ToggleSwitch.h"

#include <QAbstractButton>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace pic2pet {

namespace {

constexpr int kSidebarWidth = 190;
constexpr int kAppearanceCoalesceMs = 50;

/** 侧栏每项的图标色（苹果那种一个分类一个色） */
const QColor kNavColors[] = {
    QColor(0x4C, 0x9A, 0xFF),   // 素材
    QColor(0xAF, 0x52, 0xDE),   // 动作
    QColor(0x34, 0xC7, 0x59),   // 输入
    QColor(0xFF, 0x9F, 0x0A),   // 动态效果
    QColor(0x5A, 0xC8, 0xFA),   // 外观
    QColor(0x8E, 0x8E, 0x93),   // 通用
};

QLabel* label(const QString& text, const QString& objectName) {
    auto* l = new QLabel(text);
    l->setObjectName(objectName);
    return l;
}

QFrame* card() {
    auto* f = new QFrame;
    f->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    return f;
}

QFrame* separator() {
    auto* s = new QFrame;
    s->setObjectName(QStringLiteral("sep"));
    s->setFixedHeight(1);
    return s;
}

void addRow(QFrame* c, const QString& title, const QString& sub, QWidget* right,
            bool last = false) {
    auto* root = qobject_cast<QVBoxLayout*>(c->layout());
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 11, 16, 11);
    h->setSpacing(12);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(label(title, QStringLiteral("rowTitle")));
    if (!sub.isEmpty()) {
        auto* s = label(sub, QStringLiteral("rowSub"));
        s->setWordWrap(true);
        text->addWidget(s);
    }
    h->addLayout(text, 1);
    if (right) h->addWidget(right, 0, Qt::AlignVCenter);

    root->addWidget(row);
    if (!last) root->addWidget(separator());
}

/** 整页骨架：标题 + 说明，返回页面本身（后续往它的布局里塞卡片） */
QWidget* newPage(const QString& title, const QString& subtitle) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(22, 18, 22, 18);
    v->setSpacing(0);
    v->addWidget(label(title, QStringLiteral("pageTitle")));
    v->addSpacing(3);
    auto* s = label(subtitle, QStringLiteral("pageSubtitle"));
    s->setWordWrap(true);
    v->addWidget(s);
    v->addSpacing(14);
    return page;
}

QVBoxLayout* pageLayout(QWidget* page) {
    return qobject_cast<QVBoxLayout*>(page->layout());
}

QPushButton* linkButton(const QString& text) {
    auto* b = new QPushButton(text);
    b->setObjectName(QStringLiteral("link"));
    b->setCursor(Qt::PointingHandCursor);
    b->setFlat(true);
    return b;
}

QPushButton* ghostButton(const QString& text) {
    auto* b = new QPushButton(text);
    b->setObjectName(QStringLiteral("ghost"));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPushButton* primaryButton(const QString& text) {
    auto* b = new QPushButton(text);
    b->setObjectName(QStringLiteral("primary"));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

/** 提示条：浅底圆角 + 灰字，用来放"说明/警告"这类不需要交互的话 */
QFrame* noteBox(const QString& text) {
    auto* f = new QFrame;
    f->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(14, 11, 14, 12);
    auto* l = label(text, QStringLiteral("hint"));
    l->setWordWrap(true);
    v->addWidget(l);
    return f;
}

} // namespace

SettingsWindow::SettingsWindow(Settings* settings, QWidget* parent)
    : QWidget(parent), settings_(settings) {
    setObjectName(QStringLiteral("settingsRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("Pic2Pet 设置"));
    setStyleSheet(theme::styleSheet());
    resize(820, 580);
    setMinimumSize(720, 500);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(kSidebarWidth);
    root->addWidget(sidebar);

    stack_ = new QStackedWidget;
    root->addWidget(stack_, 1);

    buildSidebar(sidebar);

    stack_->addWidget(buildMediaPage());
    stack_->addWidget(buildActionPage());
    stack_->addWidget(buildInputPage());
    stack_->addWidget(buildMotionPage());
    stack_->addWidget(buildAppearancePage());
    stack_->addWidget(buildGeneralPage());

    appearanceCoalesce_ = new QTimer(this);
    appearanceCoalesce_->setSingleShot(true);
    connect(appearanceCoalesce_, &QTimer::timeout, this, [this] {
        if (suppressSignals_ || !scaleSlider_) return;
        emit appearanceChanged(scaleSlider_->value(), opacitySlider_->value(),
                               topToggle_->isChecked(), pauseToggle_->isChecked());
    });

    refresh();
}

void SettingsWindow::buildSidebar(QWidget* sidebar) {
    auto* v = new QVBoxLayout(sidebar);
    v->setContentsMargins(10, 12, 10, 12);
    v->setSpacing(2);

    auto* head = new QWidget;
    auto* h = new QHBoxLayout(head);
    h->setContentsMargins(6, 2, 6, 0);
    h->setSpacing(8);
    auto* dot = new QWidget;
    dot->setFixedSize(20, 20);
    dot->setStyleSheet(QStringLiteral("background:#007AFF;border-radius:6px;"));
    h->addWidget(dot);
    h->addWidget(label(QStringLiteral("Pic2Pet"), QStringLiteral("rowTitle")));
    h->addStretch(1);
    v->addWidget(head);
    v->addSpacing(12);

    const struct {
        theme::Icon icon;
        QString text;
    } kNav[] = {
        { theme::Icon::Media, QStringLiteral("素材") },
        { theme::Icon::Action, QStringLiteral("动作") },
        { theme::Icon::Input, QStringLiteral("输入") },
        { theme::Icon::Motion, QStringLiteral("动态效果") },
        { theme::Icon::Appearance, QStringLiteral("外观") },
        { theme::Icon::General, QStringLiteral("通用") },
    };

    for (int i = 0; i < int(sizeof(kNav) / sizeof(kNav[0])); ++i) {
        auto* item = new SidebarItem(kNav[i].icon, kNavColors[i], kNav[i].text);
        v->addWidget(item);
        connect(item, &QAbstractButton::clicked, this, [this, i] { stack_->setCurrentIndex(i); });
        if (i == 0) item->setChecked(true);
        if (i == 1) item->setChecked(false);
    }
    v->addStretch(1);
}

QWidget* SettingsWindow::buildMediaPage() {
    auto* page = newPage(QStringLiteral("素材"),
                         QStringLiteral("拖到桌宠身上、或按下面的按钮选择，都行。只支持单个文件，"
                                        "不支持文件夹。"));

    // 当前素材
    auto* current = card();
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 14, 16, 14);
    h->setSpacing(14);

    mediaPreview_ = new QLabel;
    mediaPreview_->setFixedSize(84, 84);
    mediaPreview_->setAlignment(Qt::AlignCenter);
    mediaPreview_->setStyleSheet(
        QStringLiteral("background:#FFFFFF;border:1px solid #E5E5EA;border-radius:10px;"));
    h->addWidget(mediaPreview_, 0, Qt::AlignTop);

    auto* col = new QVBoxLayout;
    col->setSpacing(4);
    mediaInfo_ = label(QString(), QStringLiteral("rowSub"));
    mediaInfo_->setWordWrap(true);
    col->addWidget(label(QStringLiteral("当前素材"), QStringLiteral("rowTitle")));
    col->addWidget(mediaInfo_);
    h->addLayout(col, 1);

    auto* btns = new QVBoxLayout;
    btns->setSpacing(6);
    auto* choose = primaryButton(QStringLiteral("选择素材…"));
    connect(choose, &QPushButton::clicked, this, [this] { emit chooseMediaRequested(); });
    btns->addWidget(choose);
    auto* reveal = ghostButton(QStringLiteral("打开所在文件夹"));
    connect(reveal, &QPushButton::clicked, this, [this] {
        const QString path = settings_->data.lastMedia;
        if (path.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    btns->addWidget(reveal);
    h->addLayout(btns, 0);

    qobject_cast<QVBoxLayout*>(current->layout())->addWidget(row);

    auto* lay = pageLayout(page);
    lay->addWidget(current);
    lay->addSpacing(12);

    // 最近素材
    auto* recent = card();
    auto* head = new QWidget;
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(16, 11, 16, 11);
    hh->addWidget(label(QStringLiteral("最近使用"), QStringLiteral("rowTitle")));
    hh->addStretch(1);
    qobject_cast<QVBoxLayout*>(recent->layout())->addWidget(head);

    recentList_ = new QVBoxLayout;
    recentList_->setContentsMargins(0, 0, 0, 0);
    recentList_->setSpacing(0);
    auto* holder = new QWidget;
    holder->setLayout(recentList_);
    qobject_cast<QVBoxLayout*>(recent->layout())->addWidget(holder);

    lay->addWidget(recent);
    lay->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildActionPage() {
    auto* page = newPage(QStringLiteral("动作"),
                         QStringLiteral("给待机 / 单击 / 双击 / 拖拽 / 输入各自指定素材与音频。"));

    auto* c = card();
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 14, 16, 14);
    h->setSpacing(12);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(label(QStringLiteral("逐项配置"), QStringLiteral("rowTitle")));
    auto* sub = label(QStringLiteral("素材与音频互相独立；静态图片还能单独设停留时长。"
                                     "这一页的界面正在按新样式重做，暂时先开原窗口。"),
                      QStringLiteral("rowSub"));
    sub->setWordWrap(true);
    text->addWidget(sub);
    h->addLayout(text, 1);
    auto* open = primaryButton(QStringLiteral("打开动作设置…"));
    connect(open, &QPushButton::clicked, this, [this] { emit openActionSettingsRequested(); });
    h->addWidget(open, 0, Qt::AlignVCenter);
    qobject_cast<QVBoxLayout*>(c->layout())->addWidget(row);

    auto* lay = pageLayout(page);
    lay->addWidget(c);
    lay->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildInputPage() {
    auto* page = newPage(QStringLiteral("输入"),
                         QStringLiteral("让桌宠对你的键盘有反应。默认关闭，只有你主动打开才会监听。"));

    auto* c = card();
    inputToggle_ = new ToggleSwitch;
    connect(inputToggle_, &QAbstractButton::toggled, this, [this](bool on) {
        if (!suppressSignals_) emit inputModeChanged(on);
    });
    addRow(c, QStringLiteral("输入模式（跟随键盘）"),
           QStringLiteral("全局监听键盘，敲键时播放「输入」动作，并让桌宠跟着弹一下。"),
           inputToggle_);

    bubbleToggle_ = new ToggleSwitch;
    connect(bubbleToggle_, &QAbstractButton::toggled, this, [this](bool on) {
        if (!suppressSignals_) emit keyBubbleChanged(on);
    });
    addRow(c, QStringLiteral("按键提示（气泡）"),
           QStringLiteral("敲键时在桌宠上方冒个气泡显示刚按下的键。"), bubbleToggle_, true);

    auto* lay = pageLayout(page);
    lay->addWidget(c);
    lay->addSpacing(12);
    // 注意：QLabel 不解析 Markdown，这里不要用 ** 加粗（会原样显示出来）
    lay->addWidget(noteBox(QStringLiteral(
        "隐私：只读取按键的虚拟键码（用来判断按的是哪个键），不记录、不落盘、不打日志，也不联网。\n"
        "提醒：这是本项目唯一的「挂钩式」能力，会加重杀软 / SmartScreen 的误报，不用时建议关掉。")));
    lay->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildMotionPage() {
    auto* page = newPage(QStringLiteral("动态效果"),
                         QStringLiteral("不用 Live2D 也能有手感：几个通道各算各的，最后叠加。"));

    auto* c = card();
    motionSeg_ = new SegmentedControl({ QStringLiteral("关"), QStringLiteral("弱"),
                                        QStringLiteral("中"), QStringLiteral("强") });
    connect(motionSeg_, &SegmentedControl::currentChanged, this, [this](int index) {
        if (!suppressSignals_) emit motionLevelChanged(index);
    });
    addRow(c, QStringLiteral("强度"),
           QStringLiteral("呼吸 · 打字回弹 · 拖拽倾斜"), motionSeg_, true);

    auto* lay = pageLayout(page);
    lay->addWidget(c);
    lay->addSpacing(12);
    lay->addWidget(noteBox(QStringLiteral(
        "「中」「强」开着呼吸 = 一直在动，静态素材的 CPU 会从 ≈0 升到约 2%（一直在重绘）。\n"
        "想省电就选「弱」：打字回弹和拖拽倾斜都还在，只是静止时完全不重绘。")));
    lay->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildAppearancePage() {
    auto* page = newPage(QStringLiteral("外观"),
                         QStringLiteral("缩放时窗口不会重新创建，所以怎么拉都不会闪。"));

    auto* c1 = card();

    auto* scaleRow = new QWidget;
    auto* sh = new QHBoxLayout(scaleRow);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(10);
    scaleSlider_ = new QSlider(Qt::Horizontal);
    scaleSlider_->setRange(kMinScalePercent, kMaxScalePercent);
    scaleSlider_->setFixedWidth(150);
    scaleValue_ = label(QString(), QStringLiteral("rowSub"));
    scaleValue_->setFixedWidth(46);
    scaleValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sh->addWidget(scaleSlider_);
    sh->addWidget(scaleValue_);
    connect(scaleSlider_, &QSlider::valueChanged, this, [this](int v) {
        scaleValue_->setText(QStringLiteral("%1%").arg(v));
        scheduleAppearanceEmit();
    });
    addRow(c1, QStringLiteral("缩放"), QStringLiteral("桌宠在窗口里的显示比例"), scaleRow);

    auto* opacityRow = new QWidget;
    auto* oh = new QHBoxLayout(opacityRow);
    oh->setContentsMargins(0, 0, 0, 0);
    oh->setSpacing(10);
    opacitySlider_ = new QSlider(Qt::Horizontal);
    opacitySlider_->setRange(40, 100);
    opacitySlider_->setFixedWidth(150);
    opacityValue_ = label(QString(), QStringLiteral("rowSub"));
    opacityValue_->setFixedWidth(46);
    opacityValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    oh->addWidget(opacitySlider_);
    oh->addWidget(opacityValue_);
    connect(opacitySlider_, &QSlider::valueChanged, this, [this](int v) {
        opacityValue_->setText(QStringLiteral("%1%").arg(v));
        scheduleAppearanceEmit();
    });
    addRow(c1, QStringLiteral("透明度"), QString(), opacityRow);

    topToggle_ = new ToggleSwitch;
    connect(topToggle_, &QAbstractButton::toggled, this, [this] { scheduleAppearanceEmit(); });
    addRow(c1, QStringLiteral("始终置顶"), QString(), topToggle_, true);

    auto* c2 = card();

    pauseToggle_ = new ToggleSwitch;
    connect(pauseToggle_, &QAbstractButton::toggled, this, [this] { scheduleAppearanceEmit(); });
    addRow(c2, QStringLiteral("暂停动画"), QStringLiteral("画面停在当前帧，CPU 也会降下来"), pauseToggle_);

    auto* resetBtn = ghostButton(QStringLiteral("重置"));
    connect(resetBtn, &QPushButton::clicked, this, [this] { emit resetPlacementRequested(); });
    addRow(c2, QStringLiteral("重置位置与大小"), QStringLiteral("回到屏幕中央 · 100%"),
           resetBtn);

    auto* hideBtn = ghostButton(QStringLiteral("隐藏桌宠"));
    connect(hideBtn, &QPushButton::clicked, this, [this] { emit hidePetRequested(); });
    addRow(c2, QStringLiteral("隐藏桌宠"), QStringLiteral("从托盘菜单可以再叫出来"), hideBtn, true);

    auto* lay = pageLayout(page);
    lay->addWidget(c1);
    lay->addSpacing(12);
    lay->addWidget(c2);
    lay->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildGeneralPage() {
    auto* page = newPage(QStringLiteral("通用"), QStringLiteral("启动与配置。"));

    auto* c1 = card();
    autoStartToggle_ = new ToggleSwitch;
    connect(autoStartToggle_, &QAbstractButton::toggled, this, [this](bool on) {
        if (!suppressSignals_) emit autoStartChanged(on);
    });
    addRow(c1, QStringLiteral("开机自启"),
           QStringLiteral("只写入当前用户的启动项，不改系统设置"), autoStartToggle_, true);

    auto* c2 = card();
    auto* openBtn = ghostButton(QStringLiteral("打开"));
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString path = settings_->filePath();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        }
    });
    configPathLabel_ = label(QString(), QStringLiteral("rowSub"));
    configPathLabel_->setWordWrap(true);

    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(16, 11, 16, 11);
    h->setSpacing(12);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(label(QStringLiteral("配置文件"), QStringLiteral("rowTitle")));
    text->addWidget(configPathLabel_);
    h->addLayout(text, 1);
    h->addWidget(openBtn, 0, Qt::AlignVCenter);
    qobject_cast<QVBoxLayout*>(c2->layout())->addWidget(row);

    auto* c3 = card();
    addRow(c3, QStringLiteral("Pic2Pet"),
           QStringLiteral("一张图片直出桌宠 · Qt 6 + C++20 + 原生 OpenGL"), nullptr, true);

    auto* lay = pageLayout(page);
    lay->addWidget(c1);
    lay->addSpacing(12);
    lay->addWidget(c2);
    lay->addSpacing(12);
    lay->addWidget(c3);
    lay->addStretch(1);
    return page;
}

void SettingsWindow::showPage(int index) {
    if (!stack_ || index < 0 || index >= stack_->count()) return;
    stack_->setCurrentIndex(index);
    // 侧栏选中态跟着走
    const auto items = findChildren<SidebarItem*>();
    for (int i = 0; i < items.size(); ++i) items.at(i)->setChecked(i == index);
}

void SettingsWindow::scheduleAppearanceEmit() {
    if (suppressSignals_) return;
    appearanceCoalesce_->start(kAppearanceCoalesceMs);
}

void SettingsWindow::refresh() {
    suppressSignals_ = true;

    if (inputToggle_) inputToggle_->setChecked(settings_->data.inputMode);
    if (bubbleToggle_) bubbleToggle_->setChecked(settings_->data.keyBubble);
    if (motionSeg_) motionSeg_->setCurrentIndex(settings_->data.motionLevel, false);

    if (scaleSlider_) {
        scaleSlider_->setValue(settings_->data.scalePercent);
        scaleValue_->setText(QStringLiteral("%1%").arg(settings_->data.scalePercent));
    }
    if (opacitySlider_) {
        const int pct = int(settings_->data.opacity * 100.0 + 0.5);
        opacitySlider_->setValue(pct);
        opacityValue_->setText(QStringLiteral("%1%").arg(pct));
    }
    if (topToggle_) topToggle_->setChecked(settings_->data.alwaysOnTop);
    if (configPathLabel_) configPathLabel_->setText(settings_->filePath());

    refreshMedia();
    refreshRecent();

    suppressSignals_ = false;
}

void SettingsWindow::refreshMedia() {
    const QString path = settings_->data.lastMedia;
    if (path.isEmpty()) {
        mediaPreview_->setPixmap(QPixmap());
        mediaPreview_->setText(QStringLiteral("无"));
        mediaInfo_->setText(QStringLiteral("还没有素材。拖一张图片或动图到桌宠身上就能用。"));
        return;
    }

    QPixmap pm(path);
    if (pm.isNull()) {
        mediaPreview_->setText(QStringLiteral("?"));
        mediaInfo_->setText(QStringLiteral("%1（无法预览）").arg(QFileInfo(path).fileName()));
        return;
    }
    mediaPreview_->setText(QString());
    mediaPreview_->setPixmap(pm.scaled(mediaPreview_->size() - QSize(12, 12),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));

    const QFileInfo fi(path);
    mediaInfo_->setText(QStringLiteral("%1\n%2 × %3 · %4 KB")
                            .arg(fi.fileName())
                            .arg(pm.width())
                            .arg(pm.height())
                            .arg(fi.size() / 1024));
}

void SettingsWindow::refreshRecent() {
    if (!recentList_) return;
    while (QLayoutItem* item = recentList_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    const QStringList recent = settings_->data.recentMedia;
    if (recent.isEmpty()) {
        auto* empty = label(QStringLiteral("还没有记录。"), QStringLiteral("rowSubDim"));
        empty->setContentsMargins(16, 2, 16, 12);
        recentList_->addWidget(empty);
        return;
    }

    for (int i = 0; i < recent.size(); ++i) {
        const QString path = recent.at(i);
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 9, 16, 9);
        h->setSpacing(10);
        auto* name = label(QFileInfo(path).fileName(), QStringLiteral("rowTitle"));
        h->addWidget(name, 1);
        auto* use = linkButton(QStringLiteral("使用"));
        connect(use, &QPushButton::clicked, this, [this, path] { emit recentMediaChosen(path); });
        h->addWidget(use, 0);
        recentList_->addWidget(row);
        if (i != recent.size() - 1) recentList_->addWidget(separator());
    }
}

void SettingsWindow::setAutoStartState(bool supported, bool enabled) {
    if (!autoStartToggle_) return;
    suppressSignals_ = true;
    autoStartToggle_->setEnabled(supported);
    autoStartToggle_->setChecked(enabled);
    suppressSignals_ = false;
}

void SettingsWindow::setPausedState(bool paused) {
    if (!pauseToggle_) return;
    suppressSignals_ = true;
    pauseToggle_->setChecked(paused);
    suppressSignals_ = false;
}

} // namespace pic2pet
