# BQ27220 fuel gauge on the ESP-Mosaico; INA226 monitor on the Stack-chan;
# a "battery unknown" stand-in elsewhere.
if(IDF_TARGET STREQUAL "esp32s31")
    idf_component_register(SRCS "app_power_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c log freertos bq27220 i2c_bus)
elseif(IDF_TARGET STREQUAL "esp32s3")
    idf_component_register(SRCS "app_power_hw_cores3.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
else()
    idf_component_register(SRCS "app_power_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
endif()
