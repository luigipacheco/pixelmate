// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "raylib_lite_video.h"

typedef struct sw_video sw_video_t;

/* Side of the game on the panel, centred on the round glass. 480 = 1:1,
 * crisp pixel art (the round layout keeps the corners empty). 328 would fit
 * the whole square inside the circle, but nearest-neighbour scaling drops
 * every third row and column of the pixel art. A multiple of 8. */
#ifndef SW_VIEW_SIDE
#define SW_VIEW_SIDE 480U
#endif

/** Power must be up (sw_power_init) first. */
raylib_lite_result_t sw_video_open(sw_video_t **out);
raylib_lite_video_backend_t sw_video_backend(sw_video_t *video);
/** Panel pixel (from the touch) to game pixel; clamps to the view square. */
bool sw_video_map_touch(int32_t *x, int32_t *y);
raylib_lite_result_t sw_video_close(sw_video_t *video, uint32_t timeout_ms);
