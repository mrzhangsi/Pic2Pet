#pragma once

#include <QHash>
#include <QWidget>

#include "ActionSet.h"   // PetAction
#include "Icons.h"
#include "Settings.h"
#include "TeacherMonitor.h"   // TeacherStatus

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
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

    /** AI 教官：控制器喂进来的运行态与实时状态（跨线程信号到达后调用） */
    void setTeacherRunning(bool running);
    void setTeacherStatus(const TeacherStatus& status);

    /** 网站白名单：控制器喂进来的最近一次错误（空 = 正常）。refresh 后会再调一次 */
    void setWebFilterError(const QString& error);

    /** 网站白名单：提权等待中。期间禁用开关并在状态行说明，避免连点开一堆 UAC */
    void setWebFilterBusy(bool busy);

    /**
     * 网站白名单：注册表里策略是否真的挂着。
     * 与 settings 里的 enabled 是**两回事** —— enabled 是"用户想要什么"，
     * 这个是"现在实际是什么"。进程被强杀时两者会不一致（策略还在但软件没了），
     * 正常退出则相反（enabled 记着 true、策略已被清除）。UI 要如实区分。
     */
    void setWebFilterPolicyActive(bool active);

    /** 切到某一页（0 素材 / 1 动作 / 2 输入 / 3 动态效果 / 4 外观 / 5 通用 / 6 AI 教官） */
    void showPage(int index);

signals:
    void inputModeChanged(bool on);
    void keyBubbleChanged(bool on);
    void motionLevelChanged(int level);
    void chooseMediaRequested();
    void recentMediaChosen(const QString& path);

    // ---- AI 教官页：只发意图，落盘与启停都由控制器做 ----
    /** 目标改了（运行中也生效：下一次判定就用新目标） */
    void teacherGoalEdited(const QString& goal);
    /** 模型服务改了。key 为空 = 不动原来的 key（不会把空值写回去） */
    void teacherProviderEdited(const QString& baseUrl, const QString& model, const QString& key);
    void teacherCaptureChanged(bool monitorCamera, bool uploadCamera);
    void teacherPomodoroChanged(bool enabled, int focusMinutes, int breakMinutes);
    void teacherStartRequested();
    void teacherStopRequested();
    /** 网站白名单开关（Edge）。控制器负责落盘 + 写策略 + 失败回滚 */
    void teacherWebFilterToggled(bool on);
    /** 白名单条目整表替换（增 / 删都走这个信号） */
    void teacherWebFilterEntriesChanged(const QStringList& entries);

    // ---- 动作页：逐项改动 ----
    void actionMediaChosen(PetAction action, const QString& path);
    void actionAudioChosen(PetAction action, const QString& path);
    void actionHoldChanged(PetAction action, int holdMs);
    void actionCleared(PetAction action);
    void previewActionRequested(PetAction action);
    void actionsResetRequested();
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
    struct ActionRow {
        QLabel* thumb = nullptr;
        QLabel* sub = nullptr;
        QSpinBox* hold = nullptr;
        QPushButton* clear = nullptr;
    };

    QWidget* buildActionPage();
    void refreshActions();
    void chooseActionMedia(PetAction a);
    void chooseActionAudio(PetAction a);
    /** 该动作实际会用的素材（待机没单独指定时回退到"当前素材"） */
    QString effectiveMediaFor(PetAction a) const;
    static bool isAnimatedMedia(const QString& path);
    QWidget* buildInputPage();
    QWidget* buildMotionPage();
    QWidget* buildAppearancePage();
    QWidget* buildGeneralPage();
    /** AI 教官页。内容比一屏高，放进 QScrollArea（参考实现同样栽过这个跟头） */
    QScrollArea* buildTeacherPage();
    void refreshTeacher();
    void updateTeacherStatusUi();
    /** 网站白名单：按 settings 重建条目列表（行内删除） */
    void rebuildWebFilterList();
    /** 网站白名单：刷新状态行（生效中 / 未开启 / 失败原因） */
    void updateWebFilterStatus();
    /** 网站白名单：把输入框里的条目加进列表并上报 */
    void addWebFilterEntry();
    /** 网站白名单：删掉第 index 个条目并上报 */
    void removeWebFilterEntry(int index);
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

    // ---- AI 教官页 ----
    QLabel* teacherStateLabel_ = nullptr;      // 大状态字（学习中 / 分心 / …）
    QLabel* teacherStatsLabel_ = nullptr;      // 已运行 · 采样 · 告警
    QLabel* teacherCostLabel_ = nullptr;       // token 与预估花费
    QPushButton* teacherStartBtn_ = nullptr;
    QPushButton* teacherStopBtn_ = nullptr;
    QLineEdit* teacherGoalEdit_ = nullptr;
    QLineEdit* teacherUrlEdit_ = nullptr;
    QLineEdit* teacherModelEdit_ = nullptr;
    QLineEdit* teacherKeyEdit_ = nullptr;
    ToggleSwitch* teacherCameraToggle_ = nullptr;
    ToggleSwitch* teacherUploadToggle_ = nullptr;
    ToggleSwitch* teacherPomodoroToggle_ = nullptr;
    QSpinBox* teacherFocusSpin_ = nullptr;
    QSpinBox* teacherBreakSpin_ = nullptr;
    /** 控制器喂进来的最近一次状态快照（refresh 时回灌） */
    TeacherStatus teacherStatus_;
    bool teacherRunning_ = false;
    bool teacherHasReport_ = false;   // 是否收到过结束报告（决定状态卡显示什么）

    // ---- AI 教官页 · 网站白名单（Edge）卡 ----
    ToggleSwitch* webFilterToggle_ = nullptr;
    QWidget* webFilterListPage_ = nullptr;      // 条目列表容器（随条目数重建）
    QVBoxLayout* webFilterListLayout_ = nullptr;
    QLineEdit* webFilterEntryEdit_ = nullptr;   // 新条目输入框
    QLabel* webFilterStatusLabel_ = nullptr;    // 状态行（生效中 / 未开启 / 失败原因）
    QString webFilterErrorText_;                // 控制器喂进来的错误（空 = 正常）
    bool webFilterBusy_ = false;                // 提权等待中：禁用开关
    bool policyStillActive_ = false;            // 控制器查过注册表：策略实际是否挂着

    QHash<int, ActionRow> actionRows_;   // key = int(PetAction)

    // 滑块拖动时合并上报：50ms 内的连续变化只发一次，避免一边拖一边疯狂写 INI
    QTimer* appearanceCoalesce_ = nullptr;
    bool suppressSignals_ = false;      // refresh() 期间不要回抛信号
};

} // namespace pic2pet
