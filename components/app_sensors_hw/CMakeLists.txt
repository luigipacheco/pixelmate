idf_component_register(SRCS "app_sensors_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_board_manager esp_driver_i2c esp_timer)
