#include "ActionSettingsDialog.h"

#include "Settings.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pic2pet {

namespace {

QString actionHint(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return QStringLiteral("循环");
    case PetAction::Click:       return QStringLiteral("点一下播一次");
    case PetAction::DoubleClick: return QStringLiteral("双击播一次");
    case PetAction::Drag:        return QStringLiteral("拖拽时循环");
    }
    return QString();
}

QString mediaFilter() {
    return QStringLiteral(
        "图片与动图 (*.gif *.png *.apng *.jpg *.jpeg *.bmp *.webp);;所有文件 (*.*)");
}

} // namespace

ActionSettingsDialog::ActionSettingsDialog(Settings* settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    // 普通窗口：桌宠是"无边框 + 透明 + 置顶 + Tool"，这个窗口不该沾那些属性
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("Pic2Pet · 动作设置"));
    setMinimumWidth(700);
    buildUi();
}

void ActionSettingsDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* tip = new QLabel(
        QStringLiteral("给每个动作单独指定素材。留空则沿用「宠物文件夹」里的命名约定"
                       "（idle / click / doubleclick / drag）。改动立即生效。"),
        this);
    tip->setWordWrap(true);
    root->addWidget(tip);

    auto* grid = new QGridLayout();
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);
    root->addLayout(grid);

    const PetAction kAll[] = { PetAction::Idle, PetAction::Click,
                               PetAction::DoubleClick, PetAction::Drag };

    int r = 0;
    for (PetAction a : kAll) {
        auto* name = new QLabel(
            QStringLiteral("<b>%1</b><br/><span style='color:#888'>%2</span>")
                .arg(ActionSet::actionName(a), actionHint(a)),
            this);
        name->setMinimumWidth(92);
        grid->addWidget(name, r, 0);

        auto* src = new QLabel(this);
        src->setTextInteractionFlags(Qt::TextSelectableByMouse);
        src->setMinimumWidth(260);
        sourceLabels_.insert(int(a), src);
        grid->addWidget(src, r, 1);

        auto* pickFile = new QPushButton(QStringLiteral("文件…"), this);
        auto* pickDir = new QPushButton(QStringLiteral("文件夹…"), this);
        auto* preview = new QPushButton(QStringLiteral("试播"), this);
        auto* clear = new QPushButton(QStringLiteral("清除"), this);
        for (QPushButton* b : { pickFile, pickDir, preview, clear }) b->setAutoDefault(false);

        connect(pickFile, &QPushButton::clicked, this, [this, a] { chooseFile(a); });
        connect(pickDir, &QPushButton::clicked, this, [this, a] { chooseFolder(a); });
        connect(preview, &QPushButton::clicked, this, [this, a] { emit previewRequested(a); });
        connect(clear, &QPushButton::clicked, this, [this, a] { clearAction(a); });

        auto* btns = new QHBoxLayout();
        btns->setSpacing(6);
        btns->addWidget(pickFile);
        btns->addWidget(pickDir);
        btns->addWidget(preview);
        btns->addWidget(clear);
        grid->addLayout(btns, r, 2);

        refreshRow(a);
        ++r;
    }

    root->addStretch(1);

    auto* bottom = new QHBoxLayout();
    auto* reset = new QPushButton(QStringLiteral("全部恢复为文件夹约定"), this);
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

QString& ActionSettingsDialog::field(PetAction a) {
    switch (a) {
    case PetAction::Idle:        return settings_->data.actionIdle;
    case PetAction::Click:       return settings_->data.actionClick;
    case PetAction::DoubleClick: return settings_->data.actionDoubleClick;
    case PetAction::Drag:        return settings_->data.actionDrag;
    }
    return settings_->data.actionIdle;   // 不可达
}

void ActionSettingsDialog::refreshRow(PetAction a) {
    QLabel* src = sourceLabels_.value(int(a));
    if (!src) return;

    const QString path = field(a);
    if (path.isEmpty()) {
        src->setText(QStringLiteral("<span style='color:#888'>（沿用宠物文件夹约定）</span>"));
        src->setToolTip(QString());
        return;
    }
    src->setText(QFileInfo(path).fileName());
    src->setToolTip(path);
}

void ActionSettingsDialog::applyChoice(PetAction a, const QString& path) {
    field(a) = path;
    settings_->save();
    refreshRow(a);
    emit actionsChanged();
}

void ActionSettingsDialog::chooseFile(PetAction a) {
    const QString& cur = field(a);
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("为「%1」选择素材").arg(ActionSet::actionName(a)), cur, mediaFilter());
    if (path.isEmpty()) return;
    applyChoice(a, path);
}

void ActionSettingsDialog::chooseFolder(PetAction a) {
    const QString& cur = field(a);
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("为「%1」选择序列文件夹").arg(ActionSet::actionName(a)), cur);
    if (dir.isEmpty()) return;
    applyChoice(a, dir);
}

void ActionSettingsDialog::clearAction(PetAction a) {
    if (field(a).isEmpty()) return;
    field(a).clear();
    settings_->save();
    refreshRow(a);
    emit actionsChanged();
}

void ActionSettingsDialog::resetAll() {
    bool changed = false;
    for (PetAction a : { PetAction::Idle, PetAction::Click, PetAction::DoubleClick, PetAction::Drag }) {
        if (!field(a).isEmpty()) {
            field(a).clear();
            changed = true;
        }
        refreshRow(a);
    }
    if (!changed) return;
    settings_->save();
    emit actionsChanged();
}

} // namespace pic2pet
