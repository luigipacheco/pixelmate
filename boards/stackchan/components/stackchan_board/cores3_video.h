// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "raylib_lite_video.h"

typedef struct cores3_video cores3_video_t;

raylib_lite_result_t cores3_video_open(
    esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io,
    uint16_t logical_width, uint16_t logical_height, bool swap_bytes,
    cores3_video_t **out);
raylib_lite_video_backend_t cores3_video_backend(cores3_video_t *video);
raylib_lite_result_t cores3_video_close(cores3_video_t *video, uint32_t timeout_ms);
/* False when the physical touch falls within a letterbox bar. */
bool cores3_video_map_touch(const cores3_video_t *video, int32_t *x, int32_t *y);
