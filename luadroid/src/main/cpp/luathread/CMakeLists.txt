cmake_minimum_required(VERSION 3.22.1)

project(luathread)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUATHREAD_SOURCES
        auxiliar.c
        luathread.c
        pt.c
        srm.c
)

add_library(luathread SHARED
        ${LUATHREAD_SOURCES}
)

# 包含目录 - 需要能找到 lua.h 和 ltconf.h
target_include_directories(luathread PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua
)

# 编译定义
target_compile_definitions(luathread PRIVATE
        LUA_USER_H="ltconf.h"
        LUA_USE_LINUX
        LUA_USE_DLOPEN
        LUA_COMPAT_MODULE
)

# 链接 lua 库和系统库
target_link_libraries(luathread
        lua
        log
        m
        dl
)
