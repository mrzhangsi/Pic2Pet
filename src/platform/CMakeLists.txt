add_library(pic2pet_platform STATIC
    ClickThrough.h
    ClickThrough.cpp
    AudioPlayer.h
    AudioPlayer.cpp
    HitRegions.h
    HitRegions.cpp
    PerfProbe.h
    PerfProbe.cpp
)

target_include_directories(pic2pet_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pic2pet_platform
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        Qt6::Widgets
)

if(WIN32)
    # 音频播放走 Win32 MCI（mciSendStringW）。刻意不用 Qt Multimedia：
    # 它在 Windows 默认用 FFmpeg 后端，会给发布包带进 ~90MB 的 av*.dll。
    target_link_libraries(pic2pet_platform PRIVATE winmm)
endif()

add_library(Pic2Pet::platform ALIAS pic2pet_platform)
