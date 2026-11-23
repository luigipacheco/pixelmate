// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "raylib_lite_video.h"

typedef struct sw_video sw_video_t;

/** Power must be up (sw_power_init) first. */
raylib_lite_result_t sw_video_open(sw_video_t **out);
raylib_lite_video_backend_t sw_video_backend(sw_video_t *video);
raylib_lite_result_t sw_video_close(sw_video_t *video, uint32_t timeout_ms);
