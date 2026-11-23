// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The board's two BMM150 magnetometers (I2C 0x11 / 0x12 on the shared sensor
 * bus), read raw for the TOOLS compass and metal detector. The Board Manager
 * does not create them, so this talks to them directly, like app_power_hw
 * does for the fuel gauge. Raw, uncompensated units (~0.3 uT per count). */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { APP_MAGNETOMETERS = 2 };

/** Power up and start the magnetometers; returns how many answered. */
int app_sensors_hw_mag_start(void);

/** Put both magnetometers into suspend (about 1 uA each) before a deep sleep.
 *  They keep that state while the rail stays on; mag_start wakes them. */
void app_sensors_hw_mag_suspend(void);

/** Latest raw field of magnetometer `index` (x, y, z). */
esp_err_t app_sensors_hw_mag_read(int index, int16_t xyz[3]);

#ifdef __cplusplus
}
#endif
