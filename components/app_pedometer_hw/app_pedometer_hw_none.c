// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards without a step counter wired up (the Stack-chan build):
 * the runtime sees "no hardware pedometer" and the desk pet ignores steps. */
#include "app_pedometer_hw.h"

esp_err_t app_pedometer_hw_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_pedometer_hw_read(uint32_t *steps)
{
    (void)steps;
    return ESP_ERR_NOT_SUPPORTED;
}
