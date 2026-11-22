idf_component_register(SRCS "app_game_runtime.c" "pet_view.c" "pet_screens.c" "pet_panels.c" "pet_arcade.c" "pet_grid.c" "pet_bits.c" "pet_booth.c" "booth_media.c" "pet_eyes.c" "pet_head.c" "pixel_draw.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES examples_common
    PRIV_REQUIRES app_game app_photo app_photo_store app_camera_hw app_cam_stream app_net app_neck_hw app_pedometer_hw app_power_hw app_rtc_hw app_sfx raylib-lite-engine nvs_flash esp_hw_support esp_timer esp_driver_gpio log)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RAYLIB_LITE_GAME_NATIVE=1)
if(IDF_TARGET STREQUAL "esp32s31")
    # Pocket mode (light sleep, woken by the AI button on GPIO7) is written for
    # the ESP-Mosaico. On the Stack-chan GPIO7 is the neck-servo line.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_POCKET_MODE=1)
else()
    # Every other board is a desk pet (the Stack-chan build): no step counting,
    # daily health paused, pats on the face earn sparks.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_DESK_PET=1)
endif()
