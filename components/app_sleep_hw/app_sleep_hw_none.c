// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards without a deep-sleep path (pixelmate_part(SLEEP none)):
 * entering fails, so the runtime stays awake or dozes instead. */
#include "app_sleep_hw.h"

const uint8_t app_sleep_probe_addrs[] = {0};
const int app_sleep_probe_count = 0;

esp_err_t app_sleep_hw_enter(const app_sleep_request_t *request)
{
    (void)request;
    return ESP_ERR_NOT_SUPPORTED;
}

bool app_sleep_hw_take_steps(uint32_t *delta)
{
    (void)delta;
    return false;
}

bool app_sleep_hw_report(app_sleep_report_t *out)
{
    (void)out;
    return false;
}
