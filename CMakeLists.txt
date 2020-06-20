cmake_minimum_required(VERSION 2.8)
project(gbemu CXX)

find_package(SFML COMPONENTS system window graphics audio REQUIRED)
message("SFML incs: ${SFML_INCLUDES}")
message("SFML libs: ${SFML_LIBRARIES}")

add_subdirectory(external/fmtlib)
add_subdirectory(lib/imgui)
add_subdirectory(src)
