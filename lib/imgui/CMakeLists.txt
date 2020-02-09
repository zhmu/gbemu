set(IMGUI_PATH ../../external/imgui)
include_directories(${IMGUI_PATH} ${IMGUI_PATH}/examples/libs/gl3w)
add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GL3W)
add_library(imgui ${IMGUI_PATH}/imgui.cpp ${IMGUI_PATH}/imgui_draw.cpp ${IMGUI_PATH}/imgui_widgets.cpp ${IMGUI_PATH}/examples/imgui_impl_sdl.cpp ${IMGUI_PATH}/examples/imgui_impl_opengl3.cpp ${IMGUI_PATH}/examples/libs/gl3w/GL/gl3w.c)

