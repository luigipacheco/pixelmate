# Face detection for boards with a camera and a neck to aim (M5Stack CoreS3 /
# StackChan, esp32s3); every other board gets calls that do nothing.
if(IDF_TARGET STREQUAL "esp32s3")
    idf_component_register(SRCS "app_face_hw_cores3.cpp"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES app_camera_hw app_photo human_face_detect esp-dl freertos heap log esp_timer)
else()
    idf_component_register(SRCS "app_face_hw_none.c"
        INCLUDE_DIRS "include")
endif()
