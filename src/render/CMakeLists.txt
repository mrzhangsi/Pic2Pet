add_library(pic2pet_render STATIC
    PetRenderer.h
    PetRenderer.cpp
)

target_include_directories(pic2pet_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pic2pet_render
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Pic2Pet::media
)

add_library(Pic2Pet::render ALIAS pic2pet_render)
