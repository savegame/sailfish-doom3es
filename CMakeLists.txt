cmake_minimum_required(VERSION 3.19)

if(SAILFISHOS)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

project(sailfish-doom3es)


if(NOT SFOS_PACKAGE_NAME)
set(SFOS_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
endif()

if(SAILFISHOS OR STATIC_SDL2)
    message("Configure SDL2:")
    set(LIB_SUFFIX "")
    set(PULSEAUDIO ON)
    set(SDL_STATIC ON)
    set(VIDEO_WAYLAND ON)
    set(VIDEO_X11 OFF)
    set(WAYLAND_LIBDECOR OFF)
    set(SDL_OSS OFF)
    set(STATIC_SDL2 ON)
    add_subdirectory(${CMAKE_SOURCE_DIR}/SDL2)
else()
    message("Use dynamic linked SDL2")
endif()

message("Configure imgui:")
add_subdirectory(${CMAKE_SOURCE_DIR}/imgui)

message("Configure DHEWM3:")
set(DHEWM3BINARY ${SFOS_PACKAGE_NAME})
# set(SAILFISHOS ON)
add_subdirectory(${CMAKE_SOURCE_DIR}/neo)

if(SAILFISHOS)
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
endif()
