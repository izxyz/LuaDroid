cmake_minimum_required(VERSION 3.22.1)

project(luadroid)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# luadroid 源文件
set(LUADROID_SOURCES
        farmhash.cpp
        java_type.cpp
        luadroid.cpp
        script_context.cpp
        utf8.cpp
)

# 创建 luadroid 动态库
add_library(luadroid SHARED
        ${LUADROID_SOURCES}
)

# 包含目录
target_include_directories(luadroid PRIVATE
        ../lua
        ../luathread
)

# 链接库
target_link_libraries(luadroid
        lua
        log
        android
)

