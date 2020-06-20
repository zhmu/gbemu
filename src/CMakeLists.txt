add_definitions(-std=c++17)
add_executable(gbemu main.cpp memory.cpp io.cpp video.cpp cartridge.cpp audio.cpp gui.cpp bootstrap_rom.cpp)

#include_directories(../external/imgui ../external/imgui/examples ../external/imgui/examples/libs/gl3w)
include_directories(../external/imgui ../external/imgui/examples ../external/imgui-sfml)
add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GL3W)

target_link_libraries(gbemu fmt::fmt)
target_link_libraries(gbemu imgui)
target_link_libraries(gbemu sfml-window sfml-audio sfml-graphics sfml-system)
target_link_libraries(gbemu dl GLX)
target_link_libraries(gbemu GL)
target_link_libraries(gbemu ${SFML_LIBRARIES})