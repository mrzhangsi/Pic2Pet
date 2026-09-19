#pragma once

#include <QDialog>
#include <QHash>

#include "ActionSet.h"
#include "Settings.h"

class QGroupBox;
class QLabel;
class QSpinBox;

namespace pic2pet {

/**
 * 动作设置窗口：给 待机 / 单击 / 双击 / 拖拽 各自配置
 *   - 素材文件（图片 / 动图）
 *   - 音频文件（**与素材完全独立**，可以只配音频）
 *   - 停留时长（仅在素材是**静态图片**时生效）
 *
 * 任何改动都会立刻写盘并即时生效。待机行留空表示沿用"当前素材"（lastMedia）。
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
    struct Row {
        QLabel* mediaLabel = nullptr;
        QLabel* audioLabel = nullptr;
        QSpinBox* hold = nullptr;
    };

    void buildUi();
    QGroupBox* buildGroup(PetAction a);
    ActionConfig& config(PetAction a);
    /** 该动作实际会用的素材（待机为空时回退到"当前素材"） */
    QString effectiveMedia(PetAction a) const;
    void refreshRow(PetAction a);
    void chooseMedia(PetAction a);
    void chooseAudio(PetAction a);
    void clearAction(PetAction a);
    void resetAll();
    /** 用 QImageReader 判断是不是多帧动图（动图不需要"停留时长"） */
    static bool isAnimated(const QString& path);

    Settings* settings_ = nullptr;
    QHash<int, Row> rows_;   // key = int(PetAction)
};

} // namespace pic2pet
