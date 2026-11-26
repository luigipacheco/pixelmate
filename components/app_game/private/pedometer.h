// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Software step detector over accelerometer magnitude. Private to app_game.
 * Rejects a single bump: a walk must show PEDO_CONFIRM_STEPS (2) regular peaks,
 * which are then all credited, and each further peak counts immediately. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float smooth;          /* Low-passed magnitude, g. */
    float baseline;        /* Slow gravity estimate, g. */
    bool armed;            /* Ready for the next peak. */
    bool walking;          /* Confirmed rhythm in progress. */
    bool primed;           /* Filters initialized with the first sample. */
    uint8_t pending;       /* Peaks waiting for confirmation. */
    uint32_t last_peak_tick;
} pedometer_t;

void pedometer_reset(pedometer_t *pedo);
/** Process one sample taken at model tick `tick`; returns steps to credit (0..N). */
uint32_t pedometer_sample(pedometer_t *pedo, float x_g, float y_g, float z_g, uint32_t tick);
