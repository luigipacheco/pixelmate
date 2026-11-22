idf_component_register(SRCS "app_game_runtime.c" "pet_view.c" "pet_screens.c" "pet_panels.c" "pet_arcade.c" "pet_grid.c" "pet_bits.c" "pet_minis.c" "pet_clock.c" "pet_tools.c" "pet_booth.c" "booth_media.c" "pet_eyes.c" "pixel_draw.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES examples_common
    PRIV_REQUIRES app_game app_photo app_photo_store app_camera_hw app_cam_stream app_net app_pedometer_hw app_sensors_hw app_interact_hw app_power_hw app_sfx raylib-lite-engine nvs_flash esp_hw_support esp_timer esp_driver_gpio log)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RAYLIB_LITE_GAME_NATIVE=1)
