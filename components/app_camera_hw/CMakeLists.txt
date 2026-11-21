idf_component_register(SRCS "app_camera_hw.c"
    INCLUDE_DIRS "include"
    REQUIRES app_photo
    PRIV_REQUIRES esp_driver_cam esp_driver_jpeg esp_driver_gpio esp_driver_i2c esp_hal_usb efuse esp_board_manager freertos log)

# SC101IOT camera boards need the BSP's PCLK-delay fix in the managed
# esp_cam_sensor (managed components are only changed through patches). Same
# idempotent hook as esp-mosaico-bsp's mosaico_module_camera, which this app
# does not build. --ignore-whitespace: Windows checkouts may give the patch CRLF.
set(APP_CAMERA_SENSOR_PATCH
    "${CMAKE_CURRENT_LIST_DIR}/../../../../submodule/esp-mosaico-bsp/components/mosaico_module_camera/patches/esp-cam-sensor-sc101iot-pclk-delay.patch")
find_program(APP_CAMERA_GIT git)
if(APP_CAMERA_GIT AND EXISTS "${APP_CAMERA_SENSOR_PATCH}" AND IS_DIRECTORY "${PROJECT_DIR}/managed_components/espressif__esp_cam_sensor")
    execute_process(
        COMMAND "${APP_CAMERA_GIT}" -C "${PROJECT_DIR}" apply --no-index --ignore-whitespace --reverse --check "${APP_CAMERA_SENSOR_PATCH}"
        RESULT_VARIABLE APP_CAMERA_PATCH_APPLIED OUTPUT_QUIET ERROR_QUIET)
    if(NOT APP_CAMERA_PATCH_APPLIED STREQUAL "0")
        execute_process(
            COMMAND "${APP_CAMERA_GIT}" -C "${PROJECT_DIR}" apply --no-index --ignore-whitespace "${APP_CAMERA_SENSOR_PATCH}"
            RESULT_VARIABLE APP_CAMERA_PATCH_RESULT ERROR_VARIABLE APP_CAMERA_PATCH_ERROR)
        if(APP_CAMERA_PATCH_RESULT STREQUAL "0")
            message(STATUS "app_camera_hw: applied SC101IOT PCLK delay patch")
        else()
            message(WARNING "app_camera_hw: SC101IOT patch not applied: ${APP_CAMERA_PATCH_ERROR}")
        endif()
    endif()
else()
    message(WARNING "app_camera_hw: SC101IOT PCLK patch skipped (git, patch or esp_cam_sensor missing)")
endif()
