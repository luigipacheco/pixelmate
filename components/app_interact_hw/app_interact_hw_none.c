// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards without the Mosaico Interaction module slot
 * (pixelmate_part(INTERACT none)): never present. */
#include "app_interact_hw.h"

bool app_interact_hw_poll(void)
{
    return false;
}

bool app_interact_hw_present(void)
{
    return false;
}

esp_err_t app_interact_hw_read(app_interact_inputs_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_interact_hw_leds(const uint8_t rgb[APP_INTERACT_LEDS][3])
{
    (void)rgb;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_interact_hw_ir_nec(uint8_t address, uint8_t command)
{
    (void)address;
    (void)command;
    return ESP_ERR_NOT_SUPPORTED;
}
