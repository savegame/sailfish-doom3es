cmake_minimum_required(VERSION 3.19)
project(sailfish-doom3es)

if(NOT SFOS_PACKAGE_NAME)
set(SFOS_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
endif()

message("Configure SDL2:")
set(LIB_SUFFIX "")
set(PULSEAUDIO ON)
set(SDL_STATIC ON)
set(VIDEO_WAYLAND ON)
set(VIDEO_X11 OFF)
set(WAYLAND_LIBDECOR OFF)
set(SDL_OSS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/SDL2)

message("Configure imgui:")
add_subdirectory(${CMAKE_SOURCE_DIR}/imgui)

message("Configure DHEWM3:")
set(DHEWM3BINARY ${SFOS_PACKAGE_NAME})
set(SAILFISHOS ON)
add_subdirectory(${CMAKE_SOURCE_DIR}/neo)

# AuroraOS/SailfishOS specific files and resources
add_custom_target(distfiles
    SOURCES
        rpm/sailfish-doom3es.spec
        icons/86.png
        icons/108.png
        icons/128.png
        icons/172.png
        sailfish-doom3es.desktop.in
    )

install(FILES ${SFOS_PACKAGE_NAME}.desktop
    DESTINATION /usr/share/applications
)
install(FILES icons/86.png
    DESTINATION /usr/share/icons/hicolor/86x86/apps RENAME ${SFOS_PACKAGE_NAME}.png
)
install(FILES icons/108.png
    DESTINATION /usr/share/icons/hicolor/108x108/apps RENAME ${SFOS_PACKAGE_NAME}.png
)
install(FILES icons/128.png
    DESTINATION /usr/share/icons/hicolor/128x128/apps RENAME ${SFOS_PACKAGE_NAME}.png
)
install(FILES icons/172.png
    DESTINATION /usr/share/icons/hicolor/172x172/apps RENAME ${SFOS_PACKAGE_NAME}.png
)
install(DIRECTORY res
    DESTINATION /usr/share/${SFOS_PACKAGE_NAME}
)
