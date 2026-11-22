idf_component_register(SRCS "app_power_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_board_manager esp_driver_i2c log freertos bq27220 i2c_bus)
