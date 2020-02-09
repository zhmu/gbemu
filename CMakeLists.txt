cmake_minimum_required(VERSION 2.8)
project(gbemu)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_subdirectory(external/fmtlib)
add_subdirectory(lib/imgui)
add_subdirectory(src)
