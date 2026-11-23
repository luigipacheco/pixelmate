idf_component_register(SRCS "app_sleep_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_board_manager esp_driver_gpio esp_driver_i2c esp_hw_support esp_lcd esp_system efuse log nvs_flash)
