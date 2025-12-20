cmake_minimum_required(VERSION 3.22.1)

project(luazlib)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUAZLIB_SOURCES
        lua_zlib.c
)

add_library(luazlib SHARED
        ${LUAZLIB_SOURCES}
)

target_include_directories(luazlib PRIVATE
        ../lua
)
target_link_libraries(luazlib
        lua
        z
)