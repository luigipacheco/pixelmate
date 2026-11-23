// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Deep sleep for the ESP-Mosaico, as an experiment: put the display into deep
 * standby, optionally cut the board's switched 3.3 V rail, and send the chip
 * to deep sleep. Waking is a full restart. The sequence follows the BSP's
 * bsp_power_prepare_sleep / bsp_power_enter_deep_sleep; we drive the pins
 * ourselves because the game build owns the board through Board Manager.
 *
 * What we want to learn on the board: does it sleep and wake at all, what
 * stays powered with the rail cut, and does the BMI270 keep counting steps. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extra steps around the sleep, one bit each, to find the one that stops a wake. */
enum {
    APP_SLEEP_STEP_PANEL = 1,        /* Display Off + Sleep In + Deep Standby, CS floated. */
    APP_SLEEP_STEP_HOLD_RAIL = 2,    /* Hold GPIO60 (and the v1.0 codec rail). */
    APP_SLEEP_STEP_HOLD_SWITCH = 4,  /* Hold GPIO57, the shutdown line. */
    APP_SLEEP_STEP_CAPTURE = 8,      /* Read the step counter at wake, before the board init. */
};

typedef struct {
    uint32_t seconds;         /* Timer wake after this long. */
    bool rail_off;            /* Cut the switched 3.3 V rail (display, codec, ...). */
    bool touch_wake;          /* Also wake on the touch interrupt (rail on only). */
    bool plain;               /* Timer only: no panel standby, no pin holds, no rail. */
    uint8_t mask;             /* APP_SLEEP_STEP_* bits to run (plain: 0). */
    uint32_t steps;           /* Hardware step counter just before sleeping. */
    uint16_t remaining_mah;   /* Fuel gauge just before sleeping. */
    uint16_t voltage_mv;
} app_sleep_request_t;

typedef struct {
    bool valid;               /* A sleep attempt ended in this restart. */
    bool woke;                /* The restart was a deep-sleep wake (else: it crashed or was reset). */
    bool rail_off, touch_wake;
    bool v1_0;                /* Board revision 1.0 (different I2C pins, codec rail). */
    uint8_t reset_reason;     /* esp_reset_reason_t. */
    uint32_t wake_causes;     /* esp_sleep_get_wakeup_causes bit mask. */
    uint32_t asked_s, slept_s;
    uint32_t steps_before;
    int32_t steps_at_wake;    /* Raw 16-bit BMI270 count read before the board re-inits it; -1 unknown. */
    bool imu_answered_rail_off;   /* The BMI270 answered while the rail was still held off. */
    uint8_t imu_chip_id;      /* 0x24 for a BMI270. */
    uint8_t imu_status;       /* INTERNAL_STATUS: low nibble 1 = still configured (kept power). */
    uint16_t alive_rail_off;  /* Bit i set: app_sleep_probe_addrs[i] answered right after the rail was cut. */
    uint16_t mah_before, mv_before;
    bool plain;
    uint8_t mask;
    uint8_t pin57, pin60;     /* Levels just before sleeping. */
    bool from_crumb;          /* RTC record lost (power cycle): rebuilt from the flash breadcrumb. */
} app_sleep_report_t;

/** I2C addresses behind `alive_rail_off`, in bit order. */
extern const uint8_t app_sleep_probe_addrs[];
extern const int app_sleep_probe_count;

/** Sleep now. Call from the game loop with nothing left to draw. Returns only
 * when the sleep could not start. */
esp_err_t app_sleep_hw_enter(const app_sleep_request_t *request);

/** Steps the BMI270 counted during the deep sleep this boot woke from, once
 *  per wake (later restarts return false). */
bool app_sleep_hw_take_steps(uint32_t *delta);

/** The result of the last sleep attempt, captured early in this boot (kept
 * across software restarts, lost on power-off). When that record is gone, a
 * breadcrumb written to flash before sleeping still reports the attempt (as
 * not woken, with this boot's reset reason). False when there is none.
 * Call once NVS is up; the breadcrumb is cleared after the first call. */
bool app_sleep_hw_report(app_sleep_report_t *out);

#ifdef __cplusplus
}
#endif
