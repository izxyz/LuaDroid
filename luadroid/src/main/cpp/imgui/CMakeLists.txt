cmake_minimum_required(VERSION 3.22.1)

project(imgui)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(IMGUI_SOURCES
        imgui.cpp
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_impl_android.cpp
        imgui_impl_opengl3.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
)

add_library(imgui SHARED
        ${IMGUI_SOURCES}
)

target_link_libraries(imgui
        android
        EGL
        GLESv3
        log
)