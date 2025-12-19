cmake_minimum_required(VERSION 3.22.1)

project(luasocket)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUASOCKET_SOURCES
        auxiliar.c
        buffer.c
        compat.c
        except.c
        inet.c
        io.c
        luasocket.c
        options.c
        select.c
        serial.c
        tcp.c
        timeout.c
        udp.c
        usocket.c
)
set(LUAMIME_SOURCES mime.c)

add_library(luasocket SHARED
        ${LUASOCKET_SOURCES}
)
add_library(luamime SHARED
        ${LUAMIME_SOURCES}
)

target_include_directories(luasocket PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua
)
target_include_directories(luamime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua
)

target_link_libraries(luasocket
        lua
        m
)
target_link_libraries(luamime
        lua
        m
)
