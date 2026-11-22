idf_component_register(SRCS "app_interact_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES app_camera_hw esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_adc log)
