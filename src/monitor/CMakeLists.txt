# 本机行为监视（src/monitor）
#
# 目前只有一个 EdgeWatch：用 UI Automation **只读** Edge 地址栏，越过白名单就报警。
# **不改系统、不写注册表、不注入、不挂钩。** 见 README.md。

add_library(Pic2Pet_monitor STATIC
    EdgeWatch.h
    EdgeWatch.cpp
)

add_library(Pic2Pet::monitor ALIAS Pic2Pet_monitor)

target_include_directories(Pic2Pet_monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Pic2Pet_monitor
    PUBLIC
        Qt6::Core
    PRIVATE
        # UI Automation（读地址栏）：uiautomationcore 提供 CUIAutomation，
        # ole32/oleaut32 是 COM 基础。非 Windows 上这套头文件不存在，
        # EdgeWatch.cpp 里用 #ifdef Q_OS_WIN 整体隔离，非 Windows 是空壳。
        $<$<PLATFORM_ID:Windows>:uiautomationcore ole32 oleaut32 user32>
)
