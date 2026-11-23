# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(RTC ...)).
idf_build_get_property(part PIXELMATE_RTC)
if(part STREQUAL "bm8563")
    idf_component_register(SRCS "app_rtc_hw_bm8563.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_rtc_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_rtc_hw: PIXELMATE_RTC is '${part}'; expected one of: bm8563, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
