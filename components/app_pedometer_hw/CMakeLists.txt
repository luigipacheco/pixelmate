idf_component_register(SRCS "app_pedometer_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES bmi270 i2c_bus esp_board_manager)
