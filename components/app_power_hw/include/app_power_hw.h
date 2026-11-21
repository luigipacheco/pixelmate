// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Battery gauge for ESP-Mosaico. Device-only; contracts in ../README.md.
 * Call from the game loop task only. */
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t percent;      /**< State of charge, 0..100. */
    uint16_t voltage_mv;
    int16_t current_ma;   /**< Positive while charging. */
} app_battery_reading_t;

/** Read the BQ27220 fuel gauge on the shared I2C bus (address 0x55). */
esp_err_t app_power_hw_battery(app_battery_reading_t *reading);

#ifdef __cplusplus
}
#endif
