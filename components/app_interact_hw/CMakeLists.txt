# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(INTERACT ...)).
idf_build_get_property(part PIXELMATE_INTERACT)
if(part STREQUAL "mosaico_module")
    # The Mosaico Interaction module in the right slot.
    idf_component_register(SRCS "app_interact_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES app_camera_hw esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_adc log)
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_interact_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_interact_hw: PIXELMATE_INTERACT is '${part}'; expected one of: mosaico_module, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
