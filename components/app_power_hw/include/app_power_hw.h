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
    int16_t avg_current_ma;      /**< Gauge's ~1 min average (AverageCurrent); 0 if unread. */
    uint16_t time_to_empty_min;  /**< 0xFFFF while not discharging or unread. */
    uint16_t remaining_mah;      /**< RemainingCapacity; 0 if unread. */
    uint16_t full_mah;           /**< FullChargeCapacity; 0 if unread. */
    bool external_power;         /**< A supply is connected (USB); false when the driver cannot tell. */
} app_battery_reading_t;

/** Read the battery: the BQ27220 fuel gauge on the ESP-Mosaico (I2C 0x55), the
 * INA226 monitor on the StackChan body (I2C 0x41, percent estimated). */
esp_err_t app_power_hw_battery(app_battery_reading_t *reading);

#ifdef __cplusplus
}
#endif
