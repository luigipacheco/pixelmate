// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include "app_pedometer_hw.h"
#include "bmi270.h"
#include "esp_board_manager_includes.h"

/* The ESP-Mosaico board adapter owns the BMI270 and keeps sampling the
 * accelerometer from its own task. Its device handle is a board-private struct
 * whose FIRST member is `struct bmi2_dev` (see the engine's
 * examples/boards/esp-mosaico/mosaico_setup_imu.c), so the handle address is
 * also the bmi2_dev address. Re-check this when the board adapter changes.
 * Step-counter calls use the feature-page window (0x30..0x3F) and do not
 * disturb the board's accelerometer data reads. */
#define IMU_DEVICE_NAME "imu_sensor"

static struct bmi2_dev *s_sensor;   /* Borrowed from the board; never freed here. */
static bool s_started;              /* Owned by the game loop task. */

esp_err_t app_pedometer_hw_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    void *handle = NULL;
    if (esp_board_manager_get_device_handle(IMU_DEVICE_NAME, &handle) != ESP_OK || !handle) {
        return ESP_ERR_INVALID_STATE;
    }
    struct bmi2_dev *sensor = handle;
    const uint8_t features[] = {BMI2_STEP_COUNTER};
    if (bmi270_sensor_enable(features, 1, sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    s_sensor = sensor;
    s_started = true;
    return ESP_OK;
}

esp_err_t app_pedometer_hw_read(uint32_t *steps)
{
    if (!steps) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    struct bmi2_feat_sensor_data data = {.type = BMI2_STEP_COUNTER};
    if (bmi270_get_feature_data(&data, 1, s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    *steps = data.sens_data.step_counter_output;
    return ESP_OK;
}
