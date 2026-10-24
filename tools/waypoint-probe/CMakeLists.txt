cmake_minimum_required(VERSION 3.20)
project(WaypointProbe VERSION 0.4.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG 58075c4414b985b352d10718b02a8c43f25efd7c)
FetchContent_Declare(nexus GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git GIT_TAG 9b2c53df86c00db6495642bfcff2d0611bd957ef)
FetchContent_MakeAvailable(imgui nexus)

add_library(WaypointProbe SHARED src/probe.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(WaypointProbe PRIVATE ${imgui_SOURCE_DIR} ${nexus_SOURCE_DIR})
target_compile_definitions(WaypointProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(WaypointProbe PRIVATE /utf-8 /W4)
target_link_libraries(WaypointProbe PRIVATE ws2_32)
set_target_properties(WaypointProbe PROPERTIES PREFIX "" OUTPUT_NAME "WaypointProbe")

enable_testing()
add_executable(probe_smoke tests/smoke.cpp)
target_include_directories(probe_smoke PRIVATE ${nexus_SOURCE_DIR})
target_compile_options(probe_smoke PRIVATE /utf-8 /W4)
add_test(NAME probe_smoke COMMAND probe_smoke $<TARGET_FILE:WaypointProbe>)
