add_library(pic2pet_input STATIC
    KeyboardMonitor.h
    KeyboardMonitor.cpp
)

target_include_directories(pic2pet_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pic2pet_input
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::Gui
)

# WH_KEYBOARD_LL 在 user32.lib 里；顺便把 shell32 留着备用（开机自启/通知区同类 API）
if(WIN32)
    target_link_libraries(pic2pet_input PRIVATE user32)
endif()

add_library(Pic2Pet::input ALIAS pic2pet_input)
