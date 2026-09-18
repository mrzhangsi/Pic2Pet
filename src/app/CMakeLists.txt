# 链接期用 app.rc 嵌清单时，必须禁用 CMake 自动生成的默认清单（/MANIFEST:NO）。
# CMake 4.x 默认改走 mt.exe 后处理嵌默认清单（CMP0175 NEW），会覆盖我们的清单，
# 故显式回退到 OLD（链接器 /MANIFEST 路径），保证 /MANIFEST:NO 生效、app.rc 清单唯一。
if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()

add_executable(Pic2Pet
    main.cpp
    PetWindow.h
    PetWindow.cpp
    PetController.h
    PetController.cpp
    Settings.h
    Settings.cpp
    AutoStart.h
    AutoStart.cpp
    app.rc          # 链接期把 app.manifest 作为 RT_MANIFEST 焊死进 exe
)

target_link_libraries(Pic2Pet
    PRIVATE
        Pic2Pet::platform
        Pic2Pet::render
        Pic2Pet::media
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

set_target_properties(Pic2Pet PROPERTIES MACOSX_BUNDLE OFF)

if(PIC2PET_CONSOLE)
    message(STATUS "Pic2Pet: console subsystem ON (dev/debug)")
    set_target_properties(Pic2Pet PROPERTIES WIN32_EXECUTABLE OFF)
else()
    message(STATUS "Pic2Pet: console subsystem OFF (release packaging)")
    set_target_properties(Pic2Pet PROPERTIES WIN32_EXECUTABLE ON)
endif()

# 清单（app.manifest -> RT_MANIFEST）在链接期焊死进 exe，不再依赖部署阶段的 mt.exe 后处理。
# CI runner 上 mt.exe 常不在 PATH，会静默跳过 -> 发布版缺清单 -> Win10/11 下分层透明窗口变黑底。
# 必须禁用链接器自动生成的默认清单，否则与 app.rc 的 RT_MANIFEST 冲突（CVTRES CVT1100 资源重复）。
if(MSVC)
    set_target_properties(Pic2Pet PROPERTIES LINK_FLAGS "/MANIFEST:NO")
endif()
