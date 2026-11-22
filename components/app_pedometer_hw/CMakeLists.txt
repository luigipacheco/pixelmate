# The driver borrows the BMI270 that the ESP-Mosaico board adapter owns. Other
# boards (the Stack-chan build) get a stand-in that reports "no step counter".
if(IDF_TARGET STREQUAL "esp32s31")
    idf_component_register(SRCS "app_pedometer_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES bmi270 i2c_bus esp_board_manager)
else()
    idf_component_register(SRCS "app_pedometer_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
endif()
