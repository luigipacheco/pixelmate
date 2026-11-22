// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Battery-backed real-time clock, so the time survives power-off without
 * Wi-Fi. Only the M5Stack CoreS3 (StackChan) has one wired up (BM8563 at I2C
 * 0x51); other boards get calls that report ESP_ERR_NOT_SUPPORTED.
 * Device-only; call from the game loop task. The RTC holds UTC. */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set the system clock from the RTC. ESP_ERR_INVALID_STATE when the RTC lost
 * power or was never set. */
esp_err_t app_rtc_hw_load(void);

/** Write the system clock to the RTC (after an internet time sync). */
esp_err_t app_rtc_hw_store(void);

#ifdef __cplusplus
}
#endif
