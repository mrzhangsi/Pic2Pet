#pragma once

#include <QDialog>
#include <QHash>

#include "ActionSet.h"

class QLabel;

namespace pic2pet {

class Settings;

/**
 * 动作设置窗口：给 待机 / 单击 / 双击 / 拖拽 各自指定素材。
 *
 * 留空 = 沿用"宠物文件夹"里的命名约定（idle.gif / click.gif / ...）；
 * 指定了则优先用指定项。任何改动都会立刻写盘并即时生效。
 *
 * 它是**普通窗口**——桌宠本身是"无边框 + 透明 + 置顶 + Tool"，这个窗口不继承那些属性；
 * 并且按需创建、关闭即释放（见 PetController::openActionSettings）。
 */
class ActionSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ActionSettingsDialog(Settings* settings, QWidget* parent = nullptr);

signals:
    /** 任意一项被改动：控制器据此重建动作集并立即生效 */
    void actionsChanged();
    /** 请求试播某个动作 */
    void previewRequested(PetAction a);

private:
    void buildUi();
    QString& field(PetAction a);
    void refreshRow(PetAction a);
    void applyChoice(PetAction a, const QString& path);
    void chooseFile(PetAction a);
    void chooseFolder(PetAction a);
    void clearAction(PetAction a);
    void resetAll();

    Settings* settings_ = nullptr;
    QHash<int, QLabel*> sourceLabels_;   // key = int(PetAction)
};

} // namespace pic2pet
