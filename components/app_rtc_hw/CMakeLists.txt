# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(RTC ...)).
idf_build_get_property(part PIXELMATE_RTC)
if(part STREQUAL "bm8563")
    idf_component_register(SRCS "app_rtc_hw_bm8563.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
elseif(part STREQUAL "rx8130")
    idf_component_register(SRCS "app_rtc_hw_rx8130.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_driver_i2c)
    idf_build_get_property(i2c_port PIXELMATE_I2C_PORT)
    if(NOT i2c_port)
        set(i2c_port 0)
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_I2C_PORT=${i2c_port})
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_rtc_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_rtc_hw: PIXELMATE_RTC is '${part}'; expected one of: bm8563, rx8130, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
