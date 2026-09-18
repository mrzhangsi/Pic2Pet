add_library(pic2pet_media STATIC
    FrameSequence.h
    FrameSequence.cpp
    CanvasCompositor.h
    CanvasCompositor.cpp
    GifDecoder.h
    GifDecoder.cpp
    ApngDecoder.h
    ApngDecoder.cpp
    MediaLoader.h
    MediaLoader.cpp
)

target_include_directories(pic2pet_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# media 层刻意不依赖任何第三方库：
# GIF 自研 LZW，APNG 自研 chunk 分割后交给 Qt 自带的 QImage。
target_link_libraries(pic2pet_media
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

add_library(Pic2Pet::media ALIAS pic2pet_media)
