# Driver chosen by the board (boards/<name>/board.cmake, pixelmate_part(SLEEP ...)).
idf_build_get_property(part PIXELMATE_SLEEP)
if(part STREQUAL "mosaico")
    # ESP-Mosaico deep sleep: panel standby, rail holds, AI-button wake
    # through a wake stub that keeps the Vibe Mode bootloader out of recovery.
    idf_component_register(SRCS "app_sleep_hw.c" "rtc_wake_stub_ai_button.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_board_manager esp_driver_gpio esp_driver_i2c esp_hw_support esp_lcd esp_system efuse log nvs_flash
                     # Register helpers for the wake stub (rtc_wake_stub_ai_button.c).
                     esp_hal_clock esp_hal_gpio esp_hal_pmu esp_hal_rtc_timer esp_hal_timg esp_hal_wdt)
elseif(part STREQUAL "m5stopwatch")
    # M5Stack StopWatch: display, touch and sound off through the M5IOE1,
    # wake on the sleep button; the BMI270 keeps counting.
    idf_component_register(SRCS "app_sleep_hw_m5stopwatch.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_hw_support esp_system log)
    idf_build_get_property(i2c_port PIXELMATE_I2C_PORT)
    idf_build_get_property(button PIXELMATE_SLEEP_BUTTON_GPIO)
    if(NOT i2c_port)
        set(i2c_port 0)
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_I2C_PORT=${i2c_port})
    if(button)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELMATE_SLEEP_BUTTON_GPIO=${button})
    endif()
elseif(part STREQUAL "none")
    idf_component_register(SRCS "app_sleep_hw_none.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common)
else()
    message(FATAL_ERROR "app_sleep_hw: PIXELMATE_SLEEP is '${part}'; expected one of: mosaico, m5stopwatch, none. Build through a board project (cmake/pixelmate_board.cmake).")
endif()
