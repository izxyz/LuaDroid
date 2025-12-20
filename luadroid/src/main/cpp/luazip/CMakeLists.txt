cmake_minimum_required(VERSION 3.22.1)

project(luazip)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUAZIP_SOURCES
        lua_zip.c
)

add_library(luazip SHARED
        ${LUAZIP_SOURCES}
)

target_include_directories(luazip PUBLIC
        ../lua
        ../zip
)
target_link_libraries(luazip
        lua
        zip
)