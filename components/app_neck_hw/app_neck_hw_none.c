// SPDX-License-Identifier: Apache-2.0
/* Boards without a neck (the ESP-Mosaico): every call is a no-op. */
#include "app_neck_hw.h"

void app_neck_hw_start(void)
{
}

void app_neck_hw_aim(float pan_deg, float tilt_deg)
{
    (void)pan_deg;
    (void)tilt_deg;
}

void app_neck_hw_rest(bool rest)
{
    (void)rest;
}

app_neck_state_t app_neck_hw_state(void)
{
    return APP_NECK_ABSENT;
}
