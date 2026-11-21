idf_component_register(SRCS "app_cam_stream.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_camera_hw esp_http_server freertos log heap)
