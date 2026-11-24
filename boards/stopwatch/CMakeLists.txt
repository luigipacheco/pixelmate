# PixelMate for the M5Stack StopWatch (C152, ESP32-S3R8, round CO5300 AMOLED).
#
# Builds the same game as the ESP-Mosaico project in the repo root (../../main
# and ../../components) with this board's parts list (board.cmake) and its own
# board adapter (components/stopwatch_board). No ESP Board Manager: the
# adapter powers the display and touch itself. See README.md.
cmake_minimum_required(VERSION 3.22)

get_filename_component(PIXELMATE_GAME_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(RAYLIB_LITE_ENGINE_ROOT
    "${PIXELMATE_GAME_DIR}/../../submodule/raylib-lite-engine" ABSOLUTE)
if(NOT EXISTS "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_common/CMakeLists.txt")
    message(FATAL_ERROR "Raylib Lite Engine not found at ${RAYLIB_LITE_ENGINE_ROOT}")
endif()

# The game's main component names its board through this variable.
set(RAYLIB_LITE_BOARD "stopwatch_board" CACHE STRING "Raylib Lite Board component" FORCE)

# Shared game code: every component in ../../components except the Mosaico
# project's generated Board Manager code.
include("${PIXELMATE_GAME_DIR}/cmake/pixelmate_board.cmake")
pixelmate_shared_component_dirs(PIXELMATE_SHARED_COMPONENTS)
list(APPEND EXTRA_COMPONENT_DIRS
    "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_common"
    "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_audio"
    "${PIXELMATE_GAME_DIR}/main"
    ${PIXELMATE_SHARED_COMPONENTS})

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "Per-build config")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
pixelmate_board(stopwatch)
project(pixelmate_stopwatch VERSION 1.0.0)
