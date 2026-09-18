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
