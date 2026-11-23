// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards whose battery gauge is not written yet (the Stack-chan
 * body has an INA226: voltage and current only). The app shows no battery. */
#include "app_power_hw.h"

esp_err_t app_power_hw_battery(app_battery_reading_t *reading)
{
    (void)reading;
    return ESP_ERR_NOT_SUPPORTED;
}

/* No fuel gauge here, so there is no battery profile to check or load. */
app_gauge_profile_t app_power_hw_profile(void)
{
    return APP_GAUGE_PROFILE_OK;
}
