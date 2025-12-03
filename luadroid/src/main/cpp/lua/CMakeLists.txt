cmake_minimum_required(VERSION 3.22.1)

project(lua)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Lua 核心源文件
set(LUA_CORE_SOURCES
        lapi.c
        lcode.c
        lctype.c
        ldebug.c
        ldo.c
        ldump.c
        lfunc.c
        lgc.c
        llex.c
        lmem.c
        lobject.c
        lopcodes.c
        lparser.c
        lstate.c
        lstring.c
        ltable.c
        ltm.c
        lundump.c
        lvm.c
        lzio.c
)

# Lua 标准库源文件
set(LUA_LIB_SOURCES
        lauxlib.c
        lbaselib.c
        lbitlib.c
        lcorolib.c
        ldblib.c
        liolib.c
        lmathlib.c
        loslib.c
        lstrlib.c
        ltablib.c
        lutf8lib.c
        loadlib.c
        linit.c
)

# 创建 Lua 动态库
add_library(lua SHARED
        ${LUA_CORE_SOURCES}
        ${LUA_LIB_SOURCES}
)

# 包含目录
# PUBLIC: 让依赖 lua 的目标也能访问这些头文件
# PRIVATE: 只在编译 lua 时使用
target_include_directories(lua PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../luathread
)

# 编译定义
target_compile_definitions(lua PRIVATE
        LUA_USER_H="ltconf.h"
        LUA_USE_LINUX
        LUA_USE_DLOPEN
        LUA_COMPAT_MODULE
)

# 链接系统库
target_link_libraries(lua
        log
        m
        dl
)

# 导出 Lua 符号
set_target_properties(lua PROPERTIES
        C_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
)
