# Battery-backed clock chip. Only the M5Stack CoreS3 build (esp32s3) uses one.
if(IDF_TARGET STREQUAL "esp32s3")
    idf_component_register(SRCS "app_rtc_hw_cores3.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
else()
    idf_component_register(SRCS "app_rtc_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
endif()
