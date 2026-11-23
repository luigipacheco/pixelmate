// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards without the ESP-Mosaico's magnetometers: the TOOLS page
 * finds none (pixelmate_part(SENSORS none)). */
#include "app_sensors_hw.h"

int app_sensors_hw_mag_start(void)
{
    return 0;
}

void app_sensors_hw_mag_suspend(void)
{
}

esp_err_t app_sensors_hw_mag_read(int index, int16_t xyz[3])
{
    (void)index;
    (void)xyz;
    return ESP_ERR_NOT_SUPPORTED;
}
