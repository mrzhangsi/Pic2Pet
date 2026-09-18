add_executable(pic2pet_mediadump
    main.cpp
)

target_link_libraries(pic2pet_mediadump
    PRIVATE
        Pic2Pet::media
        Qt6::Core
        Qt6::Gui
)
