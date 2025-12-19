cmake_minimum_required(VERSION 3.22.1)

project(luacjson)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUACJSON_SOURCES
        dtoa.c
        g_fmt.c
        lua_cjson.c
        strbuf.c
)

add_definitions(-DUSE_INTERNAL_FPCONV)
add_definitions(-DMULTIPLE_THREADS)
add_definitions(-DDISABLE_INVALID_NUMBERS)

add_library(luacjson SHARED
        ${LUACJSON_SOURCES}
)

target_include_directories(luacjson PUBLIC
        ../lua
)
target_link_libraries(luacjson
        lua
)