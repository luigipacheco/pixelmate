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
    uint16_t design_mah;         /**< DesignCapacity (80 once the profile is loaded); 0 if unread. */
} app_battery_reading_t;

/** Battery profile in the gauge (it lives in gauge RAM and is lost when the
 *  gauge resets, e.g. after the battery ran flat). */
typedef enum {
    APP_GAUGE_PROFILE_UNCHECKED = 0,
    APP_GAUGE_PROFILE_BUSY,      /**< Checking or loading; battery reads wait. */
    APP_GAUGE_PROFILE_OK,        /**< Already loaded (by us or by Vibe Mode). */
    APP_GAUGE_PROFILE_LOADED,    /**< Was missing; loaded at this boot. */
    APP_GAUGE_PROFILE_FAILED,    /**< Could not load; readings use gauge defaults. */
} app_gauge_profile_t;

/** Read the battery: the BQ27220 fuel gauge on the ESP-Mosaico (I2C 0x55; the
 *  first call starts a one-shot profile check and until it finishes this
 *  returns ESP_ERR_NOT_FINISHED), the INA226 monitor on the Stack-chan body
 *  (I2C 0x41, percent estimated). */
esp_err_t app_power_hw_battery(app_battery_reading_t *reading);

/** Where the gauge's battery profile stands. */
app_gauge_profile_t app_power_hw_profile(void);

#ifdef __cplusplus
}
#endif
