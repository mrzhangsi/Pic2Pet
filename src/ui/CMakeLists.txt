add_library(pic2pet_ui STATIC
    Theme.h
    Theme.cpp
    Icons.h
    Icons.cpp
    ToggleSwitch.h
    ToggleSwitch.cpp
    SegmentedControl.h
    SegmentedControl.cpp
    SidebarItem.h
    SidebarItem.cpp
)

target_include_directories(pic2pet_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 有意只依赖 Qt，不依赖本项目任何业务模块 —— ui 是"外观"，不该认识 Settings / Controller
target_link_libraries(pic2pet_ui
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

add_library(Pic2Pet::ui ALIAS pic2pet_ui)
