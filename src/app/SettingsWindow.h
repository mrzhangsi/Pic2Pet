#pragma once

#include <QWidget>

#include "Icons.h"
#include "Settings.h"

class QLabel;
class QSlider;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace pic2pet {

class SegmentedControl;
class SidebarItem;
class ToggleSwitch;

/**
 * 「设置」主窗口：左侧边栏 + 右侧内容（macOS 系统设置那种）。
 *
 * 职责边界：**它只负责"显示 + 发意图"，不直接改设置** ——
 * 每个改动都通过信号抛给 `PetController`，由控制器统一落盘并生效，然后回调 `refresh()`
 * 把真实状态同步回控件。这样不会出现"窗口里改了、控制器没跟上"的两套状态。
 */
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(Settings* settings, QWidget* parent = nullptr);

    /** 从 settings 重新读一遍刷新界面（外部改动后调用） */
    void refresh();

    /** 开机自启是注册表状态、不在 Settings 里，由控制器喂进来 */
    void setAutoStartState(bool supported, bool enabled);
    /** 暂停是控制器的运行态，也不在 Settings 里 */
    void setPausedState(bool paused);

    /** 切到某一页（0 素材 / 1 动作 / 2 输入 / 3 动态效果 / 4 外观 / 5 通用） */
    void showPage(int index);

signals:
    void inputModeChanged(bool on);
    void keyBubbleChanged(bool on);
    void motionLevelChanged(int level);
    void chooseMediaRequested();
    void recentMediaChosen(const QString& path);
    void openActionSettingsRequested();
    /**
     * 外观这几项改了。**值随信号一起带出去**，窗口自己**不写 settings** ——
     * 窗口只发意图，落盘与生效都由控制器做（避免两处都改状态、互相对不上）。
     */
    void appearanceChanged(int scalePercent, int opacityPercent, bool alwaysOnTop, bool paused);
    void autoStartChanged(bool on);
    void hidePetRequested();
    void resetPlacementRequested();

private:
    QWidget* buildMediaPage();
    QWidget* buildActionPage();
    QWidget* buildInputPage();
    QWidget* buildMotionPage();
    QWidget* buildAppearancePage();
    QWidget* buildGeneralPage();
    void buildSidebar(QWidget* sidebar);
    void refreshMedia();
    void refreshRecent();
    void scheduleAppearanceEmit();

    Settings* settings_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    QLabel* mediaPreview_ = nullptr;
    QLabel* mediaInfo_ = nullptr;
    QVBoxLayout* recentList_ = nullptr;

    ToggleSwitch* inputToggle_ = nullptr;
    ToggleSwitch* bubbleToggle_ = nullptr;
    SegmentedControl* motionSeg_ = nullptr;

    QSlider* scaleSlider_ = nullptr;
    QLabel* scaleValue_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityValue_ = nullptr;
    ToggleSwitch* topToggle_ = nullptr;
    ToggleSwitch* pauseToggle_ = nullptr;

    ToggleSwitch* autoStartToggle_ = nullptr;
    QLabel* configPathLabel_ = nullptr;

    // 滑块拖动时合并上报：50ms 内的连续变化只发一次，避免一边拖一边疯狂写 INI
    QTimer* appearanceCoalesce_ = nullptr;
    bool suppressSignals_ = false;      // refresh() 期间不要回抛信号
};

} // namespace pic2pet
