// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <string.h>
#include "private/pedometer.h"

/* Tuned for wrist/pocket walking at 30 Hz ticks; a step is 0.3..2 s apart. */
#define PEDO_SMOOTH_ALPHA 0.35f
#define PEDO_BASELINE_ALPHA 0.03f
#define PEDO_PEAK_G 0.11f        /* Rise above gravity that marks a peak. */
#define PEDO_REARM_G 0.02f       /* Signal must fall back below this to re-arm. */
#define PEDO_MIN_GAP_TICKS 8U    /* ~0.27 s */
#define PEDO_MAX_GAP_TICKS 60U   /* ~2.0 s */
#define PEDO_CONFIRM_STEPS 2U   /* Credited together once the second one comes. */

void pedometer_reset(pedometer_t *pedo)
{
    memset(pedo, 0, sizeof(*pedo));
}

uint32_t pedometer_sample(pedometer_t *pedo, float x_g, float y_g, float z_g, uint32_t tick)
{
    const float magnitude = sqrtf(x_g * x_g + y_g * y_g + z_g * z_g);
    if (!pedo->primed) {
        pedo->smooth = magnitude;
        pedo->baseline = magnitude;
        pedo->armed = true;
        pedo->primed = true;
        return 0;
    }
    pedo->smooth += PEDO_SMOOTH_ALPHA * (magnitude - pedo->smooth);
    pedo->baseline += PEDO_BASELINE_ALPHA * (magnitude - pedo->baseline);
    const float rise = pedo->smooth - pedo->baseline;
    const uint32_t gap = tick - pedo->last_peak_tick;

    if (gap > PEDO_MAX_GAP_TICKS) {
        pedo->walking = false;
        pedo->pending = 0;
    }
    if (!pedo->armed) {
        if (rise < PEDO_REARM_G) {
            pedo->armed = true;
        }
        return 0;
    }
    if (rise < PEDO_PEAK_G || (pedo->pending + pedo->walking > 0 && gap < PEDO_MIN_GAP_TICKS)) {
        return 0;
    }
    pedo->armed = false;
    pedo->last_peak_tick = tick;
    if (pedo->walking) {
        return 1;
    }
    if (++pedo->pending >= PEDO_CONFIRM_STEPS) {
        const uint32_t confirmed = pedo->pending;
        pedo->pending = 0;
        pedo->walking = true;
        return confirmed;
    }
    return 0;
}
