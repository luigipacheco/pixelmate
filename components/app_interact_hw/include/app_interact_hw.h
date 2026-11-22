// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The Mosaico Interaction module in the RIGHT slot (the camera owns the
 * left one): two keys, a PIR motion sensor, a light sensor, six WS2812 LEDs
 * and an IR LED. A small driver of our own (pins and timings follow the BSP's
 * mosaico_module_interact), so it shares the slot bus with app_camera_hw and
 * never initializes the BSP next to Board Manager. Game-loop only. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { APP_INTERACT_LEDS = 6 };

typedef struct {
    bool left, right;      /* Keys pressed (GPIO mode: key to ground). */
    bool motion;           /* PIR output. */
    uint8_t light;         /* Relative light level 0..100 (filtered). */
} app_interact_inputs_t;

/** Probe the right slot's EEPROM (call every couple of seconds); brings the
 * module up when it appears and releases it when it goes. Returns presence. */
bool app_interact_hw_poll(void);

bool app_interact_hw_present(void);

esp_err_t app_interact_hw_read(app_interact_inputs_t *out);

/** Six RGB colors (0..255). */
esp_err_t app_interact_hw_leds(const uint8_t rgb[APP_INTERACT_LEDS][3]);

/** Send one NEC IR frame (38 kHz). */
esp_err_t app_interact_hw_ir_nec(uint8_t address, uint8_t command);

#ifdef __cplusplus
}
#endif
