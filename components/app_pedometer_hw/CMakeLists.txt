# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(PEDOMETER ...)).
idf_build_get_property(part PIXELMATE_PEDOMETER)
if(part STREQUAL "bmi270_mosaico")
    # Borrows the BMI270 that the ESP-Mosaico board adapter owns.
    idf_component_register(SRCS "app_pedometer_hw.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES bmi270 i2c_bus esp_board_manager)
elseif(part STREQUAL "bmi270")
    # A BMI270 this driver sets up itself on the board's internal I2C bus.
    idf_component_register(SRCS "app_pedometer_hw_bmi270.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES bmi270 i2c_bus esp_driver_i2c esp_rom)
    idf_build_get_property(i2c_port PIXELMATE_I2C_PORT)
    if(NOT i2c_port)
        set(i2c_port 0)
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_I2C_PORT=${i2c_port})
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_pedometer_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_pedometer_hw: PIXELMATE_PEDOMETER is '${part}'; expected one of: bmi270_mosaico, bmi270, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
