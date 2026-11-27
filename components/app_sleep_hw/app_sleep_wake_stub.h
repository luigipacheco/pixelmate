// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Deep-sleep wake stub for the AI-button wake on the ESP-Mosaico (private to
 * app_sleep_hw). The stub runs from RTC memory before the Vibe Mode
 * bootloader: it confirms the press, waits for the release and hands GPIO7
 * back to the digital GPIO matrix, so the bootloader does not see the button
 * held and boot Vibe Mode. Held past APP_WAKE_STUB_HOLD_MS, it lets go
 * anyway and the bootloader starts Vibe Mode (the rescue still works). */
#include <stdbool.h>
#include <stdint.h>

#define APP_WAKE_STUB_GPIO 7            /* The AI button: an LP pad, active low. */
#define APP_WAKE_STUB_DEBOUNCE_MS 30    /* Still down this long after the wake: a press. */
#define APP_WAKE_STUB_HOLD_MS 3000      /* Down longer than this: let Vibe Mode boot. */

typedef struct {
    uint32_t runs;          /* Times the stub ran since it was armed. */
    uint32_t resleeps;      /* Bumps it rejected (straight back to sleep). */
    uint32_t last_cause;    /* PMU wake-cause bits of the last run. */
    bool confirmed;         /* The wake that booted the app was a real press. */
    bool held;              /* ... still held after APP_WAKE_STUB_HOLD_MS. */
} app_wake_stub_result_t;

/** Arm the stub for the next deep sleep (call right before esp_deep_sleep_start). */
void app_wake_stub_arm(void);

/** What the stub saw on this boot's wake. False when it was not armed. Clears it. */
bool app_wake_stub_take(app_wake_stub_result_t *out);
