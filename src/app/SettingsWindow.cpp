#include "SettingsWindow.h"

#include "SegmentedControl.h"
#include "SidebarItem.h"
#include "TeacherStats.h"
#include "Theme.h"
#include "ToggleSwitch.h"

#include <QAbstractButton>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace pic2pet {

namespace {

constexpr int kSidebarWidth = 190;
constexpr int kAppearanceCoalesceMs = 50;
/** 动作页副标题的省略宽度（见 refreshActions 里的说明） */
constexpr int kActionSubElideWidth = 220;

/** 侧栏每项的图标色（苹果那种一个分类一个色） */
const QColor kNavColors[] = {
    QColor(0x4C, 0x9A, 0xFF),   // 素材
    QColor(0xAF, 0x52, 0xDE),   // 动作
    QColor(0x34, 0xC7, 0x59),   // 输入
    QColor(0xFF, 0x9F, 0x0A),   // 动态效果
    QColor(0x5A, 0xC8, 0xFA),   // 外观
    QColor(0x8E, 0x8E, 0x93),   // 通用
    QColor(0xFF, 0x3B, 0x30),   // AI 教官（告警红）
};

const PetAction kAllActions[] = { PetAction::Idle, PetAction::Click, PetAction::DoubleClick,
                                  PetAction::Drag, PetAction::Typing };

QString actionHint(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return QStringLiteral("循环");
    case PetAction::Click:       return QStringLiteral("点一下播一次");
    case PetAction::DoubleClick: return QStringLiteral("双击播一次");
    case PetAction::Drag:        return QStringLiteral("拖拽时循环");
    case PetAction::Typing:      return QStringLiteral("敲键盘时触发");
    }
    return QString();
}

/** 注意：不含 webp —— Qt 6.11 官方包没有 WebP 图像插件，解不了 */
QString mediaFilter() {
    return QStringLiteral(
        "图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp);;所有文件 (*.*)");
}

QString audioFilter() {
    return QStringLiteral(
        "音频 (*.wav *.mp3 *.wma *.m4a *.aac *.ogg *.mid *.midi);;所有文件 (*.*)");
}

const ActionConfig& configOf(const Settings& s, PetAction a) {
    switch (a) {
    case PetAction::Idle:        return s.data.actionIdle;
    case PetAction::Click:       return s.data.actionClick;
    case PetAction::DoubleClick: return s.data.actionDoubleClick;
    case PetAction::Drag:        return s.data.actionDrag;
    case PetAction::Typing:      return s.data.actionTyping;
    }
    return s.data.actionIdle;   // 不可达
}

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
    resize(880, 600);
    setMinimumSize(820, 520);

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
    stack_->addWidget(buildTeacherPage());

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
    // 顶部放**真正的应用图标**（来自 app.qrc 的 :/app.ico），不是色块占位。
    // 按 2 倍取图再交给 QLabel 按逻辑尺寸画，高分屏才不会糊。
    auto* logo = new QLabel;
    logo->setFixedSize(22, 22);
    const QIcon appIcon(QStringLiteral(":/app.ico"));
    if (appIcon.isNull()) {
        // 资源万一丢了也别开天窗，退回一个蓝方块
        logo->setStyleSheet(QStringLiteral("background:#007AFF;border-radius:6px;"));
    } else {
        logo->setPixmap(appIcon.pixmap(QSize(22, 22), 2.0));
    }
    h->addWidget(logo);
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
        { theme::Icon::Teacher, QStringLiteral("AI 教官") },
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
                         QStringLiteral("给每个动作单独指定素材与音频，两者互相独立——可以只配其一。"
                                        "停留时长只在素材是静态图片时生效。"));

    auto* c = card();
    auto* root = qobject_cast<QVBoxLayout*>(c->layout());

    const int count = int(sizeof(kAllActions) / sizeof(kAllActions[0]));
    for (int i = 0; i < count; ++i) {
        const PetAction a = kAllActions[i];

        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 10, 16, 10);
        h->setSpacing(12);

        ActionRow r;

        r.thumb = new QLabel;
        r.thumb->setFixedSize(40, 40);
        r.thumb->setAlignment(Qt::AlignCenter);
        r.thumb->setStyleSheet(QStringLiteral(
            "background:#FFFFFF;border:1px solid #E5E5EA;border-radius:8px;"));
        h->addWidget(r.thumb, 0, Qt::AlignVCenter);

        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        text->addWidget(label(QStringLiteral("%1（%2）")
                                  .arg(ActionSet::actionName(a), actionHint(a)),
                              QStringLiteral("rowTitle")));
        r.sub = label(QString(), QStringLiteral("rowSub"));
        text->addWidget(r.sub);
        h->addLayout(text, 1);

        r.hold = new QSpinBox;
        r.hold->setRange(kMinHoldMs, kMaxHoldMs);
        r.hold->setSingleStep(100);
        r.hold->setSuffix(QStringLiteral(" ms"));
        r.hold->setFixedWidth(84);
        r.hold->setAlignment(Qt::AlignCenter);
        r.hold->setButtonSymbols(QAbstractSpinBox::NoButtons);   // 去掉老式上下箭头
        r.hold->setToolTip(QStringLiteral("只在素材是静态图片时生效：显示这么久，然后回待机。\n"
                                          "可以直接输入，也可以在框上滚轮调整。"));
        connect(r.hold, &QSpinBox::valueChanged, this, [this, a](int v) {
            if (!suppressSignals_) emit actionHoldChanged(a, v);
        });
        h->addWidget(r.hold, 0, Qt::AlignVCenter);

        // 4 个按钮自成一"组"：组内间距紧、与左边的数字框之间留大一点的间距，
        // 视觉上才会读成"一个数字 + 一组操作"，而不是五个零散控件。
        auto* btnGroup = new QWidget;
        auto* bh = new QHBoxLayout(btnGroup);
        bh->setContentsMargins(14, 0, 0, 0);
        bh->setSpacing(2);

        // 标签刻意不带省略号：这一行控件多，宽度很紧
        auto* choose = linkButton(QStringLiteral("素材"));
        connect(choose, &QPushButton::clicked, this, [this, a] { chooseActionMedia(a); });
        bh->addWidget(choose);

        auto* audio = linkButton(QStringLiteral("音频"));
        connect(audio, &QPushButton::clicked, this, [this, a] { chooseActionAudio(a); });
        bh->addWidget(audio);

        auto* preview = linkButton(QStringLiteral("试播"));
        connect(preview, &QPushButton::clicked, this,
                [this, a] { emit previewActionRequested(a); });
        bh->addWidget(preview);

        r.clear = linkButton(QStringLiteral("清除"));
        connect(r.clear, &QPushButton::clicked, this, [this, a] { emit actionCleared(a); });
        bh->addWidget(r.clear);

        h->addWidget(btnGroup, 0, Qt::AlignVCenter);

        actionRows_.insert(int(a), r);
        root->addWidget(row);
        if (i + 1 < count) root->addWidget(separator());
    }

    auto* tail = new QWidget;
    auto* th = new QHBoxLayout(tail);
    th->setContentsMargins(0, 0, 0, 0);
    th->setSpacing(12);
    th->addWidget(label(QStringLiteral("待机没单独指定素材时，会自动沿用「当前素材」。"),
                        QStringLiteral("hint")), 1);
    auto* reset = ghostButton(QStringLiteral("全部清除"));
    connect(reset, &QPushButton::clicked, this, [this] { emit actionsResetRequested(); });
    th->addWidget(reset, 0);

    auto* lay = pageLayout(page);
    lay->addWidget(c);
    lay->addSpacing(10);
    lay->addWidget(tail);
    lay->addStretch(1);
    return page;
}

QString SettingsWindow::effectiveMediaFor(PetAction a) const {
    const ActionConfig& cfg = configOf(*settings_, a);
    if (!cfg.media.isEmpty()) return cfg.media;
    if (a == PetAction::Idle) return settings_->data.lastMedia;   // 回退到"当前素材"
    return QString();
}

bool SettingsWindow::isAnimatedMedia(const QString& path) {
    if (path.isEmpty()) return false;
    QImageReader reader(path);
    if (!reader.canRead()) return false;
    return reader.imageCount() > 1;   // 多帧 = 动图
}

void SettingsWindow::chooseActionMedia(PetAction a) {
    const QString cur = configOf(*settings_, a).media;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("为「%1」选择素材").arg(ActionSet::actionName(a)), cur, mediaFilter());
    if (path.isEmpty()) return;             // 取消不动；要清空用「清除」
    emit actionMediaChosen(a, path);
}

void SettingsWindow::chooseActionAudio(PetAction a) {
    const QString cur = configOf(*settings_, a).audio;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("为「%1」选择音频").arg(ActionSet::actionName(a)), cur, audioFilter());
    if (path.isEmpty()) return;
    emit actionAudioChosen(a, path);
}

void SettingsWindow::refreshActions() {
    const int count = int(sizeof(kAllActions) / sizeof(kAllActions[0]));
    for (int i = 0; i < count; ++i) {
        const PetAction a = kAllActions[i];
        auto it = actionRows_.find(int(a));
        if (it == actionRows_.end()) continue;
        ActionRow& r = it.value();
        const ActionConfig& cfg = configOf(*settings_, a);

        // 缩略图
        const QString eff = effectiveMediaFor(a);
        QPixmap pm;
        if (!eff.isEmpty()) pm.load(eff);
        if (pm.isNull()) {
            r.thumb->setPixmap(QPixmap());
            r.thumb->setText(QStringLiteral("—"));
        } else {
            r.thumb->setText(QString());
            r.thumb->setPixmap(pm.scaled(r.thumb->size() - QSize(8, 8), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
        }

        // 副标题：素材 + 音频。这一列比较窄，文件名要省略，否则会被硬裁断。
        // 用**固定宽度**而不是 r.sub->width()：首次 refresh 时布局还没算完，宽度是 0，
        // 按它算出来的省略结果全是空的。窗口最小宽度 820 时这一列仍有 ~230px，220 是安全的。
        const QFontMetrics fm(r.sub->font());
        const auto elide = [&fm](const QString& name) {
            return fm.elidedText(name, Qt::ElideMiddle, kActionSubElideWidth);
        };
        QString mediaText;
        if (!cfg.media.isEmpty()) {
            mediaText = elide(QFileInfo(cfg.media).fileName());
        } else if (a == PetAction::Idle && !settings_->data.lastMedia.isEmpty()) {
            mediaText = QStringLiteral("沿用当前素材");
        } else {
            mediaText = QStringLiteral("未指定");
        }
        const QString audioText = cfg.audio.isEmpty()
                                      ? QStringLiteral("无")
                                      : elide(QFileInfo(cfg.audio).fileName());
        r.sub->setText(QStringLiteral("%1　·　音频：%2").arg(mediaText, audioText));

        // 停留：只有"静态图片"才有意义，动图不需要（按自身时长播完）
        {
            const QSignalBlocker blocker(r.hold);
            r.hold->setValue(cfg.holdMs);
            r.hold->setEnabled(!eff.isEmpty() && !isAnimatedMedia(eff));
        }
        r.clear->setEnabled(!cfg.media.isEmpty() || !cfg.audio.isEmpty());
    }
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

QScrollArea* SettingsWindow::buildTeacherPage() {
    auto* page = newPage(QStringLiteral("AI 教官"),
                         QStringLiteral("定期看你在做什么，分心了提醒你。右键菜单「AI 教官」"
                                        "可用默认配置一键启动；本页除「目标」外，改动都在下次"
                                        "启动时生效。"));

    // ---- 状态卡 ----
    auto* status = card();

    auto* stateRow = new QWidget;
    auto* sh = new QHBoxLayout(stateRow);
    sh->setContentsMargins(16, 14, 16, 14);
    sh->setSpacing(12);
    auto* stateText = new QVBoxLayout;
    stateText->setSpacing(1);
    teacherStateLabel_ = label(QStringLiteral("未启动"), QStringLiteral("teacherState"));
    stateText->addWidget(teacherStateLabel_);
    stateText->addWidget(label(QStringLiteral("学习中 / 分心 / 休息中"), QStringLiteral("rowSub")));
    sh->addLayout(stateText, 1);

    teacherStartBtn_ = primaryButton(QStringLiteral("启动"));
    connect(teacherStartBtn_, &QPushButton::clicked, this,
            [this] { emit teacherStartRequested(); });
    sh->addWidget(teacherStartBtn_, 0, Qt::AlignVCenter);
    auto* stopBtn = ghostButton(QStringLiteral("停止"));
    connect(stopBtn, &QPushButton::clicked, this, [this] { emit teacherStopRequested(); });
    sh->addWidget(stopBtn, 0, Qt::AlignVCenter);
    teacherStopBtn_ = stopBtn;
    qobject_cast<QVBoxLayout*>(status->layout())->addWidget(stateRow);
    qobject_cast<QVBoxLayout*>(status->layout())->addWidget(separator());

    teacherGoalEdit_ = new QLineEdit;
    teacherGoalEdit_->setPlaceholderText(QStringLiteral("学习"));
    teacherGoalEdit_->setMinimumWidth(220);
    connect(teacherGoalEdit_, &QLineEdit::editingFinished, this, [this] {
        if (!suppressSignals_) emit teacherGoalEdited(teacherGoalEdit_->text().trimmed());
    });
    addRow(status, QStringLiteral("学习目标"),
           QStringLiteral("告诉模型「什么才算在学」。运行中也可以改，下一次判定生效。"),
           teacherGoalEdit_);

    teacherStatsLabel_ = label(QStringLiteral("—"), QStringLiteral("rowSub"));
    teacherStatsLabel_->setWordWrap(true);
    addRow(status, QStringLiteral("本次会话"), QStringLiteral("已运行 · 采样 · 告警"),
           teacherStatsLabel_);

    teacherCostLabel_ = label(QStringLiteral("—"), QStringLiteral("rowSub"));
    addRow(status, QStringLiteral("模型花费"),
           QStringLiteral("按 DeepSeek 价目估算，高峰时段贵一倍"), teacherCostLabel_, true);

    // ---- 模型服务卡 ----
    auto* api = card();
    teacherUrlEdit_ = new QLineEdit;
    teacherUrlEdit_->setPlaceholderText(QStringLiteral("https://api.deepseek.com"));
    teacherUrlEdit_->setMinimumWidth(220);
    connect(teacherUrlEdit_, &QLineEdit::editingFinished, this, [this] {
        if (!suppressSignals_) {
            emit teacherProviderEdited(teacherUrlEdit_->text().trimmed(),
                                       teacherModelEdit_->text().trimmed(), QString());
        }
    });
    addRow(api, QStringLiteral("接口地址"), QStringLiteral("任何 OpenAI 兼容端点"),
           teacherUrlEdit_);

    teacherModelEdit_ = new QLineEdit;
    teacherModelEdit_->setPlaceholderText(QStringLiteral("deepseek-flash"));
    teacherModelEdit_->setMinimumWidth(220);
    connect(teacherModelEdit_, &QLineEdit::editingFinished, this, [this] {
        if (!suppressSignals_) {
            emit teacherProviderEdited(QString(), teacherModelEdit_->text().trimmed(), QString());
        }
    });
    addRow(api, QStringLiteral("模型"), QStringLiteral("需要原生支持图像输入"),
           teacherModelEdit_);

    teacherKeyEdit_ = new QLineEdit;
    teacherKeyEdit_->setEchoMode(QLineEdit::Password);
    teacherKeyEdit_->setMinimumWidth(220);
    teacherKeyEdit_->setToolTip(QStringLiteral("只写入本机配置文件，不写日志、不进报告。\n"
                                              "留空 = 保持原来的 key 不变。"));
    connect(teacherKeyEdit_, &QLineEdit::editingFinished, this, [this] {
        if (!suppressSignals_) {
            emit teacherProviderEdited(QString(), QString(), teacherKeyEdit_->text());
        }
    });
    addRow(api, QStringLiteral("API Key"),
           QStringLiteral("没配 key 也能跑：只走本地规则（锁屏 / 人离开）"),
           teacherKeyEdit_, true);

    // ---- 采集卡 ----
    auto* cap = card();
    teacherCameraToggle_ = new ToggleSwitch;
    connect(teacherCameraToggle_, &QAbstractButton::toggled, this, [this](bool) {
        if (!suppressSignals_) {
            emit teacherCaptureChanged(teacherCameraToggle_->isChecked(),
                                       teacherUploadToggle_->isChecked());
        }
    });
    addRow(cap, QStringLiteral("摄像头"),
           QStringLiteral("判断「人在不在、姿态像不像在学习」。只用系统 Media Foundation，"
                          "不装任何驱动"), teacherCameraToggle_);

    teacherUploadToggle_ = new ToggleSwitch;
    connect(teacherUploadToggle_, &QAbstractButton::toggled, this, [this](bool) {
        if (!suppressSignals_) {
            emit teacherCaptureChanged(teacherCameraToggle_->isChecked(),
                                       teacherUploadToggle_->isChecked());
        }
    });
    addRow(cap, QStringLiteral("上传摄像头画面"),
           QStringLiteral("关掉后摄像头只用于本地检测，模型只能看到屏幕"),
           teacherUploadToggle_, true);

    // ---- 课堂模式卡 ----
    auto* pom = card();
    teacherPomodoroToggle_ = new ToggleSwitch;
    connect(teacherPomodoroToggle_, &QAbstractButton::toggled, this, [this](bool) {
        if (!suppressSignals_) {
            emit teacherPomodoroChanged(teacherPomodoroToggle_->isChecked(),
                                        teacherFocusSpin_->value(),
                                        teacherBreakSpin_->value());
        }
    });
    addRow(pom, QStringLiteral("课堂模式（番茄钟）"),
           QStringLiteral("专注一段时间后提醒休息，休息期间完全不抓拍"),
           teacherPomodoroToggle_);

    auto* focusRow = new QWidget;
    auto* fh = new QHBoxLayout(focusRow);
    fh->setContentsMargins(0, 0, 0, 0);
    fh->setSpacing(8);
    teacherFocusSpin_ = new QSpinBox;
    teacherFocusSpin_->setRange(1, 180);
    teacherFocusSpin_->setSuffix(QStringLiteral(" 分钟"));
    teacherFocusSpin_->setFixedWidth(90);
    teacherFocusSpin_->setAlignment(Qt::AlignCenter);
    teacherFocusSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(teacherFocusSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!suppressSignals_) {
            emit teacherPomodoroChanged(teacherPomodoroToggle_->isChecked(),
                                        teacherFocusSpin_->value(),
                                        teacherBreakSpin_->value());
        }
    });
    fh->addWidget(teacherFocusSpin_);
    addRow(pom, QStringLiteral("专注时长"), QStringLiteral("到点提醒休息一下"), focusRow);

    auto* breakRow = new QWidget;
    auto* bh = new QHBoxLayout(breakRow);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(8);
    teacherBreakSpin_ = new QSpinBox;
    teacherBreakSpin_->setRange(0, 60);
    teacherBreakSpin_->setSuffix(QStringLiteral(" 分钟"));
    teacherBreakSpin_->setFixedWidth(90);
    teacherBreakSpin_->setAlignment(Qt::AlignCenter);
    teacherBreakSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    teacherBreakSpin_->setSpecialValueText(QStringLiteral("0（不休息）"));
    connect(teacherBreakSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!suppressSignals_) {
            emit teacherPomodoroChanged(teacherPomodoroToggle_->isChecked(),
                                        teacherFocusSpin_->value(),
                                        teacherBreakSpin_->value());
        }
    });
    bh->addWidget(teacherBreakSpin_);
    addRow(pom, QStringLiteral("休息时长"), QStringLiteral("休息期间不截图、不开摄像头"),
           breakRow, true);

    auto* lay = pageLayout(page);
    lay->addWidget(status);
    lay->addSpacing(12);
    lay->addWidget(api);
    lay->addSpacing(12);
    lay->addWidget(cap);
    lay->addSpacing(12);
    lay->addWidget(pom);
    lay->addSpacing(12);
    lay->addWidget(noteBox(QStringLiteral(
        "隐私：只在启动后定时截屏 / 拍照，图片不落盘、不上传除模型外的任何地方，"
        "日志只记字节数；不记录 API key。停止后本功能完全静默。\n"
        "花费：单次判定约 ¥0.0003~0.0007。持续学习时采样间隔会自动拉长到 5 分钟，"
        "学一天约 ¥0.04~0.09；「越不学习越费钱」是正常的——分心时需要密集采样来确认。")));
    lay->addStretch(1);

    // 内容比一屏高，套滚动区（参考实现的 GUI 同样栽过这个跟头）
    auto* scroll = new QScrollArea;
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void SettingsWindow::refreshTeacher() {
    if (!teacherGoalEdit_) return;   // 页还没建
    suppressSignals_ = true;

    const TeacherConfig& t = settings_->data.teacher;
    teacherGoalEdit_->setText(t.goal);
    teacherUrlEdit_->setText(t.api.baseUrl);
    teacherModelEdit_->setText(t.api.model);
    // Key 不回显原文：有值就显示掩码，空着显示占位提示
    teacherKeyEdit_->clear();
    teacherKeyEdit_->setPlaceholderText(t.api.apiKey.isEmpty()
                                            ? QStringLiteral("未配置：只走本地规则")
                                            : QStringLiteral("已保存一个 Key（输入可替换）"));
    teacherCameraToggle_->setChecked(t.monitorCamera);
    teacherUploadToggle_->setChecked(t.monitorCamera && t.uploadCamera);
    teacherPomodoroToggle_->setChecked(t.pomodoro.enabled);
    teacherFocusSpin_->setValue(int(t.pomodoro.focusMinutes));
    teacherBreakSpin_->setValue(int(t.pomodoro.breakMinutes));

    suppressSignals_ = false;
    updateTeacherStatusUi();
}

void SettingsWindow::setTeacherStatus(const TeacherStatus& status) {
    teacherStatus_ = status;
    updateTeacherStatusUi();
}

void SettingsWindow::setTeacherRunning(bool running) {
    teacherRunning_ = running;
    updateTeacherStatusUi();
}

void SettingsWindow::updateTeacherStatusUi() {
    if (!teacherStateLabel_) return;

    // 运行中锁定配置项（目标除外——它运行中可改）
    const bool lockConfig = teacherRunning_;
    teacherUrlEdit_->setEnabled(!lockConfig);
    teacherModelEdit_->setEnabled(!lockConfig);
    teacherKeyEdit_->setEnabled(!lockConfig);
    teacherCameraToggle_->setEnabled(!lockConfig);
    teacherUploadToggle_->setEnabled(!lockConfig && teacherCameraToggle_->isChecked());
    teacherPomodoroToggle_->setEnabled(!lockConfig);
    teacherFocusSpin_->setEnabled(!lockConfig);
    teacherBreakSpin_->setEnabled(!lockConfig);

    teacherStartBtn_->setVisible(!teacherRunning_);
    teacherStopBtn_->setVisible(teacherRunning_);

    const QString gray = QStringLiteral("#8E8E93");
    const QString green = QStringLiteral("#34C759");
    const QString red = QStringLiteral("#FF3B30");
    const QString amber = QStringLiteral("#FF9F0A");

    if (!teacherRunning_) {
        teacherStateLabel_->setText(QStringLiteral("未启动"));
        teacherStateLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(gray));
        teacherStatsLabel_->setText(QStringLiteral("—"));
        teacherCostLabel_->setText(QStringLiteral("—"));
        return;
    }

    QString stateText;
    QString color = gray;
    if (teacherStatus_.inBreak) {
        stateText = QStringLiteral("休息中");
        color = amber;
    } else {
        switch (teacherStatus_.state) {
        case TeacherState::Focusing:
            stateText = QStringLiteral("学习中");
            color = green;
            break;
        case TeacherState::Distracted:
            stateText = QStringLiteral("分心");
            color = red;
            break;
        case TeacherState::Unsure:
            stateText = QStringLiteral("不确定");
            color = amber;
            break;
        }
    }
    if (!teacherStatus_.reason.isEmpty()) {
        const QFontMetrics fm(teacherStateLabel_->font());
        stateText += QStringLiteral("　%1")
                         .arg(fm.elidedText(teacherStatus_.reason, Qt::ElideRight, 320));
    }
    teacherStateLabel_->setText(stateText);
    teacherStateLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(color));

    const int secs = int(teacherStatus_.elapsedS);
    const QString elapsed = QStringLiteral("%1:%2:%3")
                                .arg(secs / 3600)
                                .arg((secs % 3600) / 60, 2, 10, QLatin1Char('0'))
                                .arg(secs % 60, 2, 10, QLatin1Char('0'));
    teacherStatsLabel_->setText(
        QStringLiteral("已运行 %1 · 采样 %2 次（本地 %3 / 模型 %4）· 告警 %5 次 · 下次 %6s")
            .arg(elapsed)
            .arg(teacherStatus_.screenshots)
            .arg(teacherStatus_.localHits)
            .arg(teacherStatus_.apiCalls + teacherStatus_.apiFailed + teacherStatus_.apiSkipped)
            .arg(teacherStatus_.alerts)
            .arg(int(teacherStatus_.nextIntervalS)));

    const double cost = TeacherStats::estCostFor(teacherStatus_.promptTokens,
                                                 teacherStatus_.completionTokens);
    teacherCostLabel_->setText(
        QStringLiteral("输入 %1 / 输出 %2 tokens · 预估 ¥%3（%4）")
            .arg(teacherStatus_.promptTokens)
            .arg(teacherStatus_.completionTokens)
            .arg(cost, 0, 'f', 5)
            .arg(TeacherStats::isPeakTime() ? QStringLiteral("高峰时段")
                                            : QStringLiteral("空闲时段")));
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
    refreshActions();
    refreshTeacher();

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
