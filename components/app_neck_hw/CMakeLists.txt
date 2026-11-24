# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(NECK ...)).
idf_build_get_property(part PIXELMATE_NECK)
if(part STREQUAL "scs0009")
    # StackChan: two Feetech SCS0009 servos, power rail through the PY32 expander.
    idf_component_register(SRCS "app_neck_hw_scs0009.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES esp_board_manager esp_driver_i2c esp_driver_uart nvs_flash freertos log)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_neck_hw_none.c"
        INCLUDE_DIRS "include")
else()
    message(FATAL_ERROR "app_neck_hw: PIXELMATE_NECK is '${part}'; expected one of: scs0009, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
