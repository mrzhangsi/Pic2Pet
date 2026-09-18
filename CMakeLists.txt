cmake_minimum_required(VERSION 3.21)

project(Pic2Pet
    VERSION 0.1.0
    DESCRIPTION "Cross-platform desktop pet (Qt 6 + C++)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 开发时开着控制台看 [perf] 日志；打包发布时关掉，不然会多弹一个黑框。
option(PIC2PET_CONSOLE "以控制台子系统构建（开发调试用，会多一个黑框）" OFF)

# M0 spike 只需要 Qt 核心 + Widgets + OpenGL，不依赖任何第三方库。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

message(STATUS "Pic2Pet: Qt ${Qt6_VERSION} found at ${Qt6_DIR}")
message(STATUS "Pic2Pet: platform=${CMAKE_SYSTEM_NAME} compiler=${CMAKE_CXX_COMPILER_ID}")

add_subdirectory(src)

# 验证工具：导出素材每一帧为 PNG，用于和参考实现（PIL）做逐像素比对
add_subdirectory(tools/mediadump)
