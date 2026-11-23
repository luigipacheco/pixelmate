// SPDX-License-Identifier: Apache-2.0
#pragma once
/* BMI270 on-chip step counter. The sensor counts steps by itself, so walking is
 * counted even while the game is idle, dimmed or busy. Device-only; contracts in
 * ../README.md. Call from the game loop task only. */
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Enable the step counter on the Board Manager's "imu_sensor" device.
 * ESP_ERR_INVALID_STATE if the board has not created the IMU yet; safe to retry. */
esp_err_t app_pedometer_hw_start(void);

/** Read the cumulative step count since the sensor was configured (it restarts
 * at 0 on every boot). ESP_ERR_INVALID_STATE before a successful start. */
esp_err_t app_pedometer_hw_read(uint32_t *steps);

/** One accelerometer (g) and gyroscope (degrees/s) sample from the same IMU,
 * for the TOOLS sensor apps. ESP_ERR_INVALID_STATE before a successful start. */
esp_err_t app_pedometer_hw_motion(float accel_g[3], float gyro_dps[3]);

/** Before deep sleep: gyroscope off and the accelerometer in its low-power
 * mode (50 Hz, 2 samples averaged), which the step counter still runs on.
 * The next boot configures the sensor from scratch. */
esp_err_t app_pedometer_hw_low_power(void);

#ifdef __cplusplus
}
#endif
