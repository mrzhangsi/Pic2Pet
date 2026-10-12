# AI 教官（src/teacher）
#
# 从参考项目 AI_Teacher（Python + DeepSeek 视觉模型）移植过来的「学习状态监控」。
# 三级漏斗：L0 本地规则（零成本）→ L1 视觉模型（存疑才花钱）→ L2 状态机（抗抖动）。
# 详细设计决策见 ../../技术方案.md 第 16 节。

add_library(Pic2Pet_teacher STATIC
    TeacherConfig.h
    TeacherConfig.cpp
    TeacherCapture.h
    TeacherCapture.cpp
    TeacherCamera.h
    TeacherCamera.cpp
    TeacherRules.h
    TeacherRules.cpp
    TeacherJudge.h
    TeacherJudge.cpp
    TeacherTracker.h
    TeacherTracker.cpp
    TeacherPomodoro.h
    TeacherPomodoro.cpp
    TeacherStats.h
    TeacherStats.cpp
    TeacherMonitor.h
    TeacherMonitor.cpp
)

add_library(Pic2Pet::teacher ALIAS Pic2Pet_teacher)

target_include_directories(Pic2Pet_teacher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Pic2Pet_teacher
    PUBLIC
        Qt6::Core
        Qt6::Gui          # QImage
        Qt6::Network      # QNetworkAccessManager（HTTPS 走系统 Schannel，零额外 DLL）
    PRIVATE
        $<$<PLATFORM_ID:Windows>:mf mfplat mfreadwrite mfuuid ole32>
)

# 本模块只用 QtCore/Gui/Network，**不依赖 Widgets**——设置页在 app/SettingsWindow。
