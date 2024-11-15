cmake_minimum_required(VERSION 3.19)

if(SAILFISHOS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

project(sailfish-doom3es)


if(NOT SFOS_PACKAGE_NAME)
set(SFOS_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
endif()

message("Configure imgui:")
add_subdirectory(${CMAKE_SOURCE_DIR}/imgui)

message("Configure DHEWM3:")
set(DHEWM3BINARY ${SFOS_PACKAGE_NAME})
# set(SAILFISHOS ON)
add_subdirectory(${CMAKE_SOURCE_DIR}/neo)

if(SAILFISHOS)
    if(${SFOS_PACKAGE_NAME} STREQUAL "ru.sashikknox.prey")
        set(ICONS_DIR "prey/")
    else()
        set(ICONS_DIR "")
    endif()
    
    # AuroraOS/SailfishOS specific files and resources
    add_custom_target(distfiles
        SOURCES
            rpm/sailfish-doom3es.spec
            icons/86.png
            icons/108.png
            icons/128.png
            icons/172.png
            icons/prey/86.png
            icons/prey/108.png
            icons/prey/128.png
            icons/prey/172.png
            sailfish-doom3es.desktop.in
        )

    install(FILES ${SFOS_PACKAGE_NAME}.desktop
        DESTINATION /usr/share/applications
    )
    install(FILES icons/${ICONS_DIR}86.png
        DESTINATION /usr/share/icons/hicolor/86x86/apps RENAME ${SFOS_PACKAGE_NAME}.png
    )
    install(FILES icons/${ICONS_DIR}108.png
        DESTINATION /usr/share/icons/hicolor/108x108/apps RENAME ${SFOS_PACKAGE_NAME}.png
    )
    install(FILES icons/${ICONS_DIR}128.png
        DESTINATION /usr/share/icons/hicolor/128x128/apps RENAME ${SFOS_PACKAGE_NAME}.png
    )
    install(FILES icons/${ICONS_DIR}172.png
        DESTINATION /usr/share/icons/hicolor/172x172/apps RENAME ${SFOS_PACKAGE_NAME}.png
    )
    install(DIRECTORY res
        DESTINATION /usr/share/${SFOS_PACKAGE_NAME}
    )
endif()
