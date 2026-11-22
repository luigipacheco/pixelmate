# PixelMate for the M5Stack StackChan (CoreS3, ESP32-S3).
#
# A second firmware project that builds the same game sources as the
# ESP-Mosaico project one folder up (../main and ../components), with its own
# board adapter (components/stackchan_board) and none of the Mosaico services
# (ESP-Iris, Vibe Mode, Recovery). See README.md for the build steps.
cmake_minimum_required(VERSION 3.22)

get_filename_component(PIXELMATE_GAME_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(RAYLIB_LITE_ENGINE_ROOT
    "${PIXELMATE_GAME_DIR}/../../submodule/raylib-lite-engine" ABSOLUTE)
if(NOT EXISTS "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_common/CMakeLists.txt")
    message(FATAL_ERROR "Raylib Lite Engine not found at ${RAYLIB_LITE_ENGINE_ROOT}")
endif()

# The game's main component names its board through this variable.
set(RAYLIB_LITE_BOARD "stackchan_board" CACHE STRING "Raylib Lite Board component" FORCE)

# Shared game code. Listed one by one so the Mosaico project's generated
# Board Manager component (../components/gen_bmgr_codes) is never picked up;
# this project generates its own under components/gen_bmgr_codes.
list(APPEND EXTRA_COMPONENT_DIRS
    "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_common"
    "${RAYLIB_LITE_ENGINE_ROOT}/examples/common_components/examples_audio"
    "${PIXELMATE_GAME_DIR}/main"
    "${PIXELMATE_GAME_DIR}/components/app_game"
    "${PIXELMATE_GAME_DIR}/components/app_game_runtime"
    "${PIXELMATE_GAME_DIR}/components/app_photo"
    "${PIXELMATE_GAME_DIR}/components/app_photo_store"
    "${PIXELMATE_GAME_DIR}/components/app_camera_hw"
    "${PIXELMATE_GAME_DIR}/components/app_cam_stream"
    "${PIXELMATE_GAME_DIR}/components/app_neck_hw"
    "${PIXELMATE_GAME_DIR}/components/app_net"
    "${PIXELMATE_GAME_DIR}/components/app_pedometer_hw"
    "${PIXELMATE_GAME_DIR}/components/app_power_hw"
    "${PIXELMATE_GAME_DIR}/components/app_sfx")

# Board Manager's generated defaults enable the CoreS3 devices; ours come last
# so they win.
set(_stackchan_bmgr_defaults
    "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
if(NOT EXISTS "${_stackchan_bmgr_defaults}")
    message(FATAL_ERROR
        "Board Manager files are missing. Run once in this folder:\n"
        "  idf.py bmgr -c ./bmgr -b m5stack_cores3\n"
        "(see README.md)")
endif()
set(SDKCONFIG_DEFAULTS
    "${_stackchan_bmgr_defaults}"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "Per-build config")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pixelmate_stackchan VERSION 1.0.0)
