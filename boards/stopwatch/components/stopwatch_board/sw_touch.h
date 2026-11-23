// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** Power must be up (sw_power_init) first. */
esp_err_t sw_touch_init(void);
/** One finger in panel pixels (0..479); false when nothing touches. */
bool sw_touch_read(int32_t *x, int32_t *y);
