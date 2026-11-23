// SPDX-License-Identifier: Apache-2.0
#pragma once
/* OV3640 camera module in ESP-Mosaico's LEFT slot, hot-pluggable, reduced to
 * a 128x128 luminance feed for the photo booth. Device-only. A worker task owns
 * the slot and the camera; the game loop only picks a mode, reads presence and
 * copies frames. ../README.md has the board details (shared I2C,
 * USB-Serial/JTAG pad, orientation, hot-plug). */
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
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

/** The module slots' I2C bus (board I2C0 on v1.0, the sub-board I2C1 on
 * v1.1/1.2), created on first use. Shared with the interaction module driver.
 * Call from the game loop. */
esp_err_t app_camera_hw_slot_bus(void **bus);

/** Webcam: while on (and streaming), the worker also hardware-encodes an
 * upright APP_CAMERA_WEBCAM_W x _H color JPEG every other frame (~12 fps). */
enum { APP_CAMERA_WEBCAM_W = 240, APP_CAMERA_WEBCAM_H = 320, APP_CAMERA_JPEG_MAX = 96 * 1024 };
void app_camera_hw_set_webcam(bool on);
/** Copy the newest JPEG when its number differs from *seq; returns its size or 0. Any task. */
size_t app_camera_hw_jpeg(uint8_t *out, size_t cap, uint32_t *seq);

/** Current state; any task. */
app_camera_state_t app_camera_hw_state(void);

/** Copy the newest frame when its number differs from *seq, then update *seq:
 * three upright 128x128 planes, Y then U then V (U/V centered on 128).
 * Returns true when a frame was copied. */
enum { APP_CAMERA_FRAME_BYTES = 3 * APP_PHOTO_MAX_PIXELS };
bool app_camera_hw_frame(uint8_t yuv[APP_CAMERA_FRAME_BYTES], uint32_t *seq);

#ifdef __cplusplus
}
#endif
