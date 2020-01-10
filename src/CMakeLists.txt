add_definitions(-std=c++17)
add_executable(gbemu main.cpp memory.cpp io.cpp video.cpp cartridge.cpp)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
target_link_libraries(gbemu ${SDL2_LIBRARIES})

target_link_libraries(gbemu fmt::fmt)
