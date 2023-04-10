cmake_minimum_required(VERSION 3.19)
project(ru.sashikknox.dhewm3)

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