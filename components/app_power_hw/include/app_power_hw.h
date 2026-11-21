// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Screen power and battery gauge for ESP-Mosaico. Device-only; contracts in
 * ../README.md. Call from the game loop task only. */
#include <stdbool.h>
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

/** Turn the panel on at `brightness_pct` (1..100) or off when `on` is false.
 * ESP_ERR_INVALID_STATE until the board has created the display. */
esp_err_t app_power_hw_display(bool on, uint8_t brightness_pct);

/** Read the BQ27220 fuel gauge on the shared I2C bus (address 0x55). */
esp_err_t app_power_hw_battery(app_battery_reading_t *reading);

#ifdef __cplusplus
}
#endif
