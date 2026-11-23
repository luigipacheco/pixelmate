# PixelMate board selection.
#
# One game, many devices. Each device has a folder boards/<name>/ with a
# board.cmake that lists its parts, one driver per kind of hardware:
#
#     pixelmate_part(POWER ina226)     # battery monitor chip
#     pixelmate_part(NECK none)        # this board has no neck
#     pixelmate_feature(DESK_PET)      # game behaviour switches
#
# A firmware project includes this file after ESP-IDF's project.cmake and
# before project(), then calls pixelmate_board(<name>). The choices become ESP-IDF
# build properties (PIXELMATE_<KIND>, PIXELMATE_FEATURES, PIXELMATE_BOARD),
# which every component can read with idf_build_get_property(), even while
# ESP-IDF scans component requirements. Hardware components pick their
# source files from them; nothing picks code by chip (IDF_TARGET), because
# different boards share a chip.

get_filename_component(PIXELMATE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Every kind of part a board must declare (pixelmate_part(<KIND> none) when
# it has none). Drivers per kind, as named in the components' CMakeLists:
#   CAMERA     mosaico_slot | gc0308 | none
#   INTERACT   mosaico_module | none   (the Mosaico's Interaction module slot)
#   NECK       scs0009 | none          (pan/tilt servos)
#   NET        vibe | plain            (vibe: Wi-Fi shared with Mosaico Vibe Mode)
#   PEDOMETER  bmi270_mosaico | bmi270 | none   (bmi270: its own I2C device, port I2C_PORT)
#   POWER      bq27220 | ina226 | m5pm1 | none
#   RTC        bm8563 | rx8130 | none   (rx8130: on port I2C_PORT)
#   SENSORS    bmm150_mosaico | none   (magnetometers for the TOOLS page)
#   SLEEP      mosaico | none          (deep-sleep pocket mode)
set(PIXELMATE_PART_KINDS CAMERA INTERACT NECK NET PEDOMETER POWER RTC SENSORS SLEEP)

# Features: DESK_PET (nobody walks with it: pats earn sparks, no step UI),
# POCKET_MODE (sleep in a pocket, AI button on GPIO7: ESP-Mosaico only),
# ROUND_SCREEN (round glass: the layout keeps clear of the corners).

macro(pixelmate_part kind driver)
    if(NOT "${kind}" IN_LIST PIXELMATE_PART_KINDS)
        message(FATAL_ERROR "pixelmate_part: unknown kind '${kind}' (one of: ${PIXELMATE_PART_KINDS})")
    endif()
    idf_build_set_property(PIXELMATE_${kind} "${driver}")
endmacro()

# A board value some drivers need, e.g. pixelmate_setting(I2C_PORT 1): the
# I2C port of the internal bus for drivers that find it by port, or
# pixelmate_setting(SLEEP_BUTTON_GPIO 2): an active-low button that puts the
# pet to sleep and wakes it (like the Mosaico's AI button).
macro(pixelmate_setting name value)
    idf_build_set_property(PIXELMATE_${name} "${value}")
endmacro()

# The board's name as the About page shows it ("A DESK PET FOR <name>").
macro(pixelmate_name name)
    idf_build_set_property(PIXELMATE_NAME "${name}")
endmacro()

macro(pixelmate_feature name)
    idf_build_set_property(PIXELMATE_FEATURES "${name}" APPEND)
endmacro()

# Shared game components: every folder in components/ except the Mosaico
# project's generated Board Manager code. New shared components reach every
# board without editing each board project.
function(pixelmate_shared_component_dirs out_var)
    file(GLOB entries LIST_DIRECTORIES true "${PIXELMATE_DIR}/components/*")
    set(dirs)
    foreach(entry ${entries})
        get_filename_component(name "${entry}" NAME)
        if(IS_DIRECTORY "${entry}" AND EXISTS "${entry}/CMakeLists.txt" AND NOT name STREQUAL "gen_bmgr_codes")
            list(APPEND dirs "${entry}")
        endif()
    endforeach()
    set(${out_var} ${dirs} PARENT_SCOPE)
endfunction()

macro(pixelmate_board name)
    set(_pixelmate_board_file "${PIXELMATE_DIR}/boards/${name}/board.cmake")
    if(NOT EXISTS "${_pixelmate_board_file}")
        message(FATAL_ERROR "PixelMate board '${name}' not found: ${_pixelmate_board_file}")
    endif()
    idf_build_set_property(PIXELMATE_BOARD "${name}")
    include("${_pixelmate_board_file}")
    foreach(_pixelmate_kind ${PIXELMATE_PART_KINDS})
        idf_build_get_property(_pixelmate_driver PIXELMATE_${_pixelmate_kind})
        if(NOT _pixelmate_driver)
            message(FATAL_ERROR "Board '${name}' does not say which ${_pixelmate_kind} it has; "
                                "add pixelmate_part(${_pixelmate_kind} none) if it has none.")
        endif()
        list(APPEND _pixelmate_summary "${_pixelmate_kind}=${_pixelmate_driver}")
    endforeach()
    idf_build_get_property(_pixelmate_features PIXELMATE_FEATURES)
    message(STATUS "PixelMate board ${name}: ${_pixelmate_summary}; features: ${_pixelmate_features}")
endmacro()
