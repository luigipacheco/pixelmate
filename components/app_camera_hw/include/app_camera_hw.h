// SPDX-License-Identifier: Apache-2.0
#pragma once
/* OV3640 camera module in ESP-Mosaico's LEFT slot, hot-pluggable, reduced to
 * a 128x128 luminance feed for the photo booth. Device-only. A worker task owns
 * the slot and the camera; the game loop only picks a mode, reads presence and
 * copies frames. ../README.md has the board details (shared I2C,
 * USB-Serial/JTAG pad, orientation, hot-plug). */
#include <stdbool.h>
#include <stdint.h>
/* APP_PHOTO_MAX_SIDE/PIXELS: the feed matches the largest photo lens. */
#include "app_photo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_CAMERA_OFF = 0,        /**< Not streaming (released). */
    APP_CAMERA_STARTING,       /**< Bringing the sensor up. */
    APP_CAMERA_LIVE,           /**< Frames are arriving. */
    APP_CAMERA_ABSENT,         /**< No camera board in the left slot. */
    APP_CAMERA_ERROR,          /**< Bring-up or capture failed; retried on a mode change or replug. */
} app_camera_state_t;

typedef enum {
    APP_CAMERA_MODE_OFF = 0,   /**< Release everything; no probing (screen asleep). */
    APP_CAMERA_MODE_PROBE,     /**< Probe the slot EEPROM about once a second; camera released. */
    APP_CAMERA_MODE_STREAM,    /**< Probe, and stream while a camera is present. */
} app_camera_mode_t;

/** Choose what the worker does. Cheap; call every tick from the game loop. */
void app_camera_hw_set_mode(app_camera_mode_t mode);

/** Camera board detected by the last probes (debounced); any task. Keeps its
 * last value while the mode is OFF. */
bool app_camera_hw_present(void);

/** Current state; any task. */
app_camera_state_t app_camera_hw_state(void);

/** Copy the newest frame (row-major, upright, 8-bit luminance) when its number
 * differs from *seq, then update *seq. Returns true when a frame was copied. */
bool app_camera_hw_frame(uint8_t luma[APP_PHOTO_MAX_PIXELS], uint32_t *seq);

#ifdef __cplusplus
}
#endif
