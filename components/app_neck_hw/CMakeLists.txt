# The StackChan's two neck servos. Only the M5Stack CoreS3 build (esp32s3) has
# them; every other board gets calls that do nothing.
if(IDF_TARGET STREQUAL "esp32s3")
    idf_component_register(SRCS "app_neck_hw_cores3.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES esp_board_manager esp_driver_i2c esp_driver_uart nvs_flash freertos log)
else()
    idf_component_register(SRCS "app_neck_hw_none.c"
        INCLUDE_DIRS "include")
endif()
