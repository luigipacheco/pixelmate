# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(SENSORS ...)).
idf_build_get_property(part PIXELMATE_SENSORS)
if(part STREQUAL "bmm150_mosaico")
    # The ESP-Mosaico's two BMM150 magnetometers (TOOLS page compass/metal).
    idf_component_register(SRCS "app_sensors_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_i2c esp_timer)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_sensors_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_sensors_hw: PIXELMATE_SENSORS is '${part}'; expected one of: bmm150_mosaico, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
