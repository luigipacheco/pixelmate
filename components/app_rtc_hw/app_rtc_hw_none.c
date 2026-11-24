// SPDX-License-Identifier: Apache-2.0
/* Boards without a real-time clock chip in use (the ESP-Mosaico). */
#include "app_rtc_hw.h"

esp_err_t app_rtc_hw_load(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_rtc_hw_store(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
