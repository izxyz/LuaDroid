cmake_minimum_required(VERSION 3.22.1)

project(luassl)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUASSL_SOURCES
        config.c
        context.c
        ec.c
        options.c
        ssl.c
        x509.c
        luasocket/buffer.c
        luasocket/io.c
        luasocket/timeout.c
        luasocket/usocket.c
)

add_library(luassl SHARED
        ${LUASSL_SOURCES}
)

target_include_directories(luassl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua
        ${CMAKE_CURRENT_SOURCE_DIR}/../luasocket
)
target_link_libraries(luassl
        lua
        m
        ${CMAKE_CURRENT_SOURCE_DIR}/../../jniLibs/${ANDROID_ABI}/libcrypto.so
        ${CMAKE_CURRENT_SOURCE_DIR}/../../jniLibs/${ANDROID_ABI}/libssl.so
)