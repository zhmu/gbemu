set(IMGUI_PATH ../../external/imgui)
set(IMGUI_SFML_PATH ../../external/imgui-sfml)
add_definitions(-DIMGUI_USER_CONFIG="imconfig-custom.h")
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${IMGUI_SFML_PATH})
include_directories(${IMGUI_PATH})

add_library(imgui ${IMGUI_PATH}/imgui.cpp ${IMGUI_PATH}/imgui_draw.cpp ${IMGUI_PATH}/imgui_widgets.cpp ${IMGUI_PATH}/imgui_demo.cpp ${IMGUI_SFML_PATH}/imgui-SFML.cpp)

