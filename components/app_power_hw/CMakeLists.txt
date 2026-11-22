# BQ27220 fuel gauge on the ESP-Mosaico. Other boards (the Stack-chan build)
# get a stand-in that reports "battery unknown" until their gauge is written.
if(IDF_TARGET STREQUAL "esp32s31")
    idf_component_register(SRCS "app_power_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
else()
    idf_component_register(SRCS "app_power_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
endif()
