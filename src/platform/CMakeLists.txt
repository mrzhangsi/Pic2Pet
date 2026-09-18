add_library(pic2pet_platform STATIC
    ClickThrough.h
    ClickThrough.cpp
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

add_library(Pic2Pet::platform ALIAS pic2pet_platform)
