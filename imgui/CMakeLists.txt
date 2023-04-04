cmake_minimum_required(VERSION 3.19)
project(imgui)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SDL2_CONFIG_INCLUDE_SUFFIX "debug")
    set(SDL2_LIB_SUFFIX "d")
else()
    set(SDL2_CONFIG_INCLUDE_SUFFIX "relese")
    set(SDL2_LIB_SUFFIX "")
endif()
set(SDL2_INCLUDE_DIR 
    ${CMAKE_BINARY_DIR}/SDL2/include
    ${CMAKE_BINARY_DIR}/SDL2/include-config-${SDL2_CONFIG_INCLUDE_SUFFIX})

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR})


set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
)

add_library(${PROJECT_NAME} STATIC 
    ${IMGUI_SOURCES})

target_include_directories( ${PROJECT_NAME}
    PUBLIC ${IMGUI_DIR}
    PUBLIC ${IMGUI_DIR}/backends
    PUBLIC ${SDL2_INCLUDE_DIR}
)

target_compile_definitions(${PROJECT_NAME}  PUBLIC -DIMGUI_IMPL_OPENGL_ES2)