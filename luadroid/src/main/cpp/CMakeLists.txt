project(main)

cmake_minimum_required(VERSION 3.22.1)

add_subdirectory(zip)
add_subdirectory(imgui)

add_subdirectory(lua)
add_subdirectory(luathread)
add_subdirectory(luadroid)
add_subdirectory(luasocket)
add_subdirectory(luassl)
add_subdirectory(luaopenssl)
add_subdirectory(luacjson)
add_subdirectory(lualfs)
add_subdirectory(luazlib)
add_subdirectory(luazip)