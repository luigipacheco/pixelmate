# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(SLEEP ...)).
idf_build_get_property(part PIXELMATE_SLEEP)
if(part STREQUAL "mosaico")
    # ESP-Mosaico deep sleep: panel standby, rail holds, touch wake.
    idf_component_register(SRCS "app_sleep_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_gpio esp_driver_i2c esp_hw_support esp_lcd esp_system efuse log nvs_flash)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_sleep_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_sleep_hw: PIXELMATE_SLEEP is '${part}'; expected one of: mosaico, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
