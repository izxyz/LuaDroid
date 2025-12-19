cmake_minimum_required(VERSION 3.22.1)

project(lualfs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUALFS_SOURCES
        lfs.c
)

add_library(lualfs SHARED
        ${LUALFS_SOURCES}
)

target_include_directories(lualfs PUBLIC
        ../lua
)
target_link_libraries(lualfs
        lua
)