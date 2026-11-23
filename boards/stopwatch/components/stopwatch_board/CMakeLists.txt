idf_component_register(
    SRCS "board.c" "sw_power.c" "sw_video.c" "sw_touch.c"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_display_present examples_common examples_audio raylib-lite-engine esp_lcd esp_timer heap
    PRIV_REQUIRES freertos esp_driver_i2c esp_driver_gpio esp_driver_spi log
    # The shared audio worker calls a backend factory provided only by this
    # Board. Keep the provider object independent of archive order.
    WHOLE_ARCHIVE)
