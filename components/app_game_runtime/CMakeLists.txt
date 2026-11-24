idf_component_register(SRCS "app_game_runtime.c" "pet_view.c" "pet_screens.c" "pet_panels.c" "pet_arcade.c" "pet_grid.c" "pet_bits.c" "pet_minis.c" "pet_clock.c" "pet_tools.c" "pet_booth.c" "booth_media.c" "pet_eyes.c" "pet_head.c" "pet_diary.c" "pixel_draw.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES examples_common
    PRIV_REQUIRES app_game app_photo app_photo_store app_camera_hw app_cam_stream app_net app_neck_hw app_pedometer_hw app_sensors_hw app_interact_hw app_sleep_hw app_power_hw app_rtc_hw app_sfx raylib-lite-engine nvs_flash esp_hw_support esp_timer esp_driver_gpio log)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RAYLIB_LITE_GAME_NATIVE=1)
# Board features (boards/<name>/board.cmake: pixelmate_feature(...)).
# POCKET_MODE: sleep in a pocket, AI button on GPIO7 (ESP-Mosaico only; on the
# StackChan GPIO7 is the neck-servo line). DESK_PET: nobody walks with it, so
# no step counting or step screens, daily health paused, pats earn sparks.
idf_build_get_property(features PIXELMATE_FEATURES)
if("POCKET_MODE" IN_LIST features)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_POCKET_MODE=1)
endif()
if("ROUND_SCREEN" IN_LIST features)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_ROUND_SCREEN=1)
endif()
if("DESK_PET" IN_LIST features)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_DESK_PET=1)
endif()
# PIXELMATE_HAS_<KIND> for each part the board has (not "none"), and its name
# for the About page.
foreach(kind CAMERA INTERACT NECK PEDOMETER POWER RTC SENSORS SLEEP)
    idf_build_get_property(part PIXELMATE_${kind})
    if(part AND NOT part STREQUAL "none")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_HAS_${kind}=1)
    endif()
endforeach()
idf_build_get_property(sleep_button PIXELMATE_SLEEP_BUTTON_GPIO)
if(sleep_button)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_SLEEP_BUTTON_GPIO=${sleep_button})
endif()
idf_build_get_property(board_name PIXELMATE_NAME)
if(board_name)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "PIXELMATE_NAME=\"${board_name}\"")
endif()
