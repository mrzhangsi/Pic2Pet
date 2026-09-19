#include "ActionSettingsDialog.h"

#include "Settings.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pic2pet {

namespace {

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

QString mediaFilter() {
    // 注意：不含 webp —— Qt 6.11 官方包**没有** WebP 图像插件，解不了。
    // 要支持就得自己带 libwebp，属于后续里程碑。
    return QStringLiteral(
        "图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp);;所有文件 (*.*)");
}

QString audioFilter() {
    return QStringLiteral(
        "音频 (*.wav *.mp3 *.wma *.m4a *.aac *.ogg *.mid *.midi);;所有文件 (*.*)");
}

const PetAction kAllActions[] = { PetAction::Idle, PetAction::Click,
                                  PetAction::DoubleClick, PetAction::Drag,
                                  PetAction::Typing };

QString dim(const QString& text) {
    return QStringLiteral("<span style='color:#888'>%1</span>").arg(text);
}

} // namespace

ActionSettingsDialog::ActionSettingsDialog(Settings* settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    // 普通窗口：桌宠是"无边框 + 透明 + 置顶 + Tool"，这个窗口不该沾那些属性
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("Pic2Pet · 动作设置"));
    setMinimumWidth(560);
    buildUi();
}

void ActionSettingsDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* tip = new QLabel(
        QStringLiteral("每个动作可以单独指定：素材文件、音频文件（两者互相独立，可以只配其一），"
                       "以及静态图片的停留时长。改动立即生效。"),
        this);
    tip->setWordWrap(true);
    root->addWidget(tip);

    for (PetAction a : kAllActions) root->addWidget(buildGroup(a));

    root->addStretch(1);

    auto* bottom = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("全部清除"), this);
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this, [this] { resetAll(); });
    bottom->addWidget(reset);
    bottom->addStretch(1);

    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    bottom->addWidget(close);
    root->addLayout(bottom);
}

QGroupBox* ActionSettingsDialog::buildGroup(PetAction a) {
    auto* box = new QGroupBox(
        QStringLiteral("%1（%2）").arg(ActionSet::actionName(a), actionHint(a)), this);

    auto* grid = new QGridLayout(box);
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    Row r;

    // ---- 素材 ----
    grid->addWidget(new QLabel(QStringLiteral("素材"), box), 0, 0);
    r.mediaLabel = new QLabel(box);
    r.mediaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(r.mediaLabel, 0, 1);
    auto* mediaBtn = new QPushButton(QStringLiteral("选择文件…"), box);
    mediaBtn->setAutoDefault(false);
    connect(mediaBtn, &QPushButton::clicked, this, [this, a] { chooseMedia(a); });
    grid->addWidget(mediaBtn, 0, 2);

    // ---- 音频（与素材独立） ----
    grid->addWidget(new QLabel(QStringLiteral("音频"), box), 1, 0);
    r.audioLabel = new QLabel(box);
    r.audioLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(r.audioLabel, 1, 1);
    auto* audioBtn = new QPushButton(QStringLiteral("选择音频…"), box);
    audioBtn->setAutoDefault(false);
    connect(audioBtn, &QPushButton::clicked, this, [this, a] { chooseAudio(a); });
    grid->addWidget(audioBtn, 1, 2);

    // ---- 静态图的停留时长 ----
    auto* holdRow = new QHBoxLayout();
    holdRow->addWidget(new QLabel(QStringLiteral("停留"), box));
    r.hold = new QSpinBox(box);
    r.hold->setRange(kMinHoldMs, kMaxHoldMs);
    r.hold->setSingleStep(100);
    r.hold->setSuffix(QStringLiteral(" ms"));
    r.hold->setToolTip(QStringLiteral("只在素材是**静态图片**时生效：显示这么久，然后回待机"));
    connect(r.hold, &QSpinBox::valueChanged, this, [this, a](int v) {
        config(a).holdMs = v;
        settings_->save();
        emit actionsChanged();
    });
    holdRow->addWidget(r.hold);
    holdRow->addWidget(new QLabel(dim(QStringLiteral("（动图不需要，按自身时长播完）")), box));
    holdRow->addStretch(1);
    grid->addLayout(holdRow, 2, 0, 1, 2);

    auto* btns = new QHBoxLayout();
    btns->setSpacing(6);
    auto* preview = new QPushButton(QStringLiteral("试播"), box);
    auto* clear = new QPushButton(QStringLiteral("清除"), box);
    preview->setAutoDefault(false);
    clear->setAutoDefault(false);
    connect(preview, &QPushButton::clicked, this, [this, a] { emit previewRequested(a); });
    connect(clear, &QPushButton::clicked, this, [this, a] { clearAction(a); });
    btns->addWidget(preview);
    btns->addWidget(clear);
    grid->addLayout(btns, 2, 2);

    rows_.insert(int(a), r);
    refreshRow(a);          // 必须在 insert 之后：refreshRow 会取 rows_[a]
    return box;
}

ActionConfig& ActionSettingsDialog::config(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return settings_->data.actionIdle;
    case PetAction::Click:       return settings_->data.actionClick;
    case PetAction::DoubleClick: return settings_->data.actionDoubleClick;
    case PetAction::Drag:        return settings_->data.actionDrag; break;
    case PetAction::Typing:      return settings_->data.actionTyping; break;
    }
    return settings_->data.actionIdle;   // 不可达
}

QString ActionSettingsDialog::effectiveMedia(PetAction a) const {
    const ActionConfig* cfg = nullptr;
    switch (a) {
    case PetAction::Idle:        cfg = &settings_->data.actionIdle; break;
    case PetAction::Click:       cfg = &settings_->data.actionClick; break;
    case PetAction::DoubleClick: cfg = &settings_->data.actionDoubleClick; break;
    case PetAction::Drag:        cfg = &settings_->data.actionDrag; break;
    case PetAction::Typing:      cfg = &settings_->data.actionTyping; break;
    }
    if (cfg && !cfg->media.isEmpty()) return cfg->media;
    // 待机没单独指定素材时，实际用的是"当前素材"
    if (a == PetAction::Idle) return settings_->data.lastMedia;
    return QString();
}

bool ActionSettingsDialog::isAnimated(const QString& path) {
    if (path.isEmpty()) return false;
    QImageReader reader(path);
    if (!reader.canRead()) return false;
    return reader.imageCount() > 1;   // 多帧 = 动图
}

void ActionSettingsDialog::refreshRow(PetAction a) {
    Row& r = rows_[int(a)];
    const ActionConfig& cfg = config(a);

    // 素材
    if (!cfg.media.isEmpty()) {
        r.mediaLabel->setText(QFileInfo(cfg.media).fileName());
        r.mediaLabel->setToolTip(cfg.media);
    } else if (a == PetAction::Idle && !settings_->data.lastMedia.isEmpty()) {
        r.mediaLabel->setText(dim(QStringLiteral("%1（当前素材）")
                                      .arg(QFileInfo(settings_->data.lastMedia).fileName())));
        r.mediaLabel->setToolTip(settings_->data.lastMedia);
    } else {
        r.mediaLabel->setText(dim(QStringLiteral("（未指定）")));
        r.mediaLabel->setToolTip(QString());
    }

    // 音频
    if (cfg.audio.isEmpty()) {
        r.audioLabel->setText(dim(QStringLiteral("（无）")));
        r.audioLabel->setToolTip(QString());
    } else {
        r.audioLabel->setText(QFileInfo(cfg.audio).fileName());
        r.audioLabel->setToolTip(cfg.audio);
    }

    // 停留时长：只有"静态图片"才有意义
    if (r.hold) {
        const QSignalBlocker blocker(r.hold);   // 别把 setValue 当成用户改动
        r.hold->setValue(cfg.holdMs);
        const QString media = effectiveMedia(a);
        r.hold->setEnabled(!media.isEmpty() && !isAnimated(media));
    }
}

void ActionSettingsDialog::chooseMedia(PetAction a) {
    const QString cur = config(a).media;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("为「%1」选择素材").arg(ActionSet::actionName(a)), cur, mediaFilter());
    if (path.isEmpty()) return;

    config(a).media = path;
    settings_->save();
    refreshRow(a);
    emit actionsChanged();
}

void ActionSettingsDialog::chooseAudio(PetAction a) {
    const QString cur = config(a).audio;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("为「%1」选择音频").arg(ActionSet::actionName(a)), cur, audioFilter());
    if (path.isEmpty()) return;

    config(a).audio = path;
    settings_->save();
    refreshRow(a);
    emit actionsChanged();
}

void ActionSettingsDialog::clearAction(PetAction a) {
    ActionConfig& cfg = config(a);
    if (cfg.media.isEmpty() && cfg.audio.isEmpty()) return;

    cfg.media.clear();
    cfg.audio.clear();
    settings_->save();
    refreshRow(a);
    emit actionsChanged();
}

void ActionSettingsDialog::resetAll() {
    bool changed = false;
    for (PetAction a : kAllActions) {
        ActionConfig& cfg = config(a);
        if (!cfg.media.isEmpty() || !cfg.audio.isEmpty()) {
            cfg.media.clear();
            cfg.audio.clear();
            changed = true;
        }
        refreshRow(a);
    }
    if (!changed) return;
    settings_->save();
    emit actionsChanged();
}

} // namespace pic2pet
