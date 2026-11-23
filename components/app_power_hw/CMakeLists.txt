# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(POWER ...)).
idf_build_get_property(part PIXELMATE_POWER)
if(part STREQUAL "bq27220")
    # ESP-Mosaico: BQ27220 fuel gauge (loads its battery profile).
    idf_component_register(SRCS "app_power_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c log freertos bq27220 i2c_bus)
elseif(part STREQUAL "ina226")
    # INA226 voltage/current monitor (StackChan body), percent estimated.
    idf_component_register(SRCS "app_power_hw_ina226.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c)
elseif(part STREQUAL "m5pm1")
    # M5PM1 power chip (M5Stack StopWatch): battery and USB voltage.
    idf_component_register(SRCS "app_power_hw_m5pm1.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_driver_i2c)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_power_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_power_hw: PIXELMATE_POWER is '${part}'; expected one of: bq27220, ina226, m5pm1, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
