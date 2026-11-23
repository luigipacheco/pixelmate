// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Pure nearest-neighbor RGB565 strip extraction for the CoreS3 SPI presenter.
 * Kept independent from ESP-IDF so Host tests check the actual pixel path. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cores3_viewport.h"

/* Rows [top, top+count) of the viewport only (view_width pixels each, packed),
 * for frames that skip the black bars. An exact 2:1 reduction (480 logical
 * to 240) takes every other pixel without a division per pixel. */
static inline void cores3_fill_view_rows(
    const cores3_viewport_t *view, const uint16_t *frame, uint16_t *strip,
    uint16_t top, uint16_t count)
{
    const uint32_t logical_w = view->logical_width;
    const uint32_t logical_h = view->logical_height;
    const uint32_t w = view->view_width;
    const bool half = logical_w == 2U * w && logical_h == 2U * view->view_height;
    for (uint32_t row = 0; row < count; row++) {
        const uint32_t vy = (uint32_t)top + row;
        uint16_t *dst = strip + row * w;
        const uint32_t src_y = half ? vy * 2U : (vy * logical_h) / view->view_height;
        const uint16_t *src = frame + src_y * logical_w;
        if (half) {
            for (uint32_t x = 0; x < w; x++) dst[x] = src[x * 2U];
        } else {
            for (uint32_t x = 0; x < w; x++) dst[x] = src[(x * logical_w) / w];
        }
    }
}

static inline void cores3_fill_strip_pixels(
    const cores3_viewport_t *view, const uint16_t *frame, uint16_t *strip,
    uint16_t top, uint16_t count, bool swap_bytes)
{
    const uint32_t logical_w = view->logical_width;
    const uint32_t logical_h = view->logical_height;
    for (uint32_t row = 0; row < count; row++) {
        uint32_t y = (uint32_t)top + row;
        uint16_t *dst = strip + row * view->lcd_width;
        if (y < view->view_y ||
                y >= (uint32_t)view->view_y + view->view_height) {
            memset(dst, 0, view->lcd_width * sizeof(uint16_t));
            continue;
        }
        uint32_t src_y = ((y - view->view_y) * logical_h) / view->view_height;
        const uint16_t *src = frame + src_y * logical_w;
        for (uint32_t x = 0; x < view->lcd_width; x++) {
            uint16_t color = 0;
            if (x >= view->view_x &&
                    x < (uint32_t)view->view_x + view->view_width) {
                uint32_t src_x = ((x - view->view_x) * logical_w) / view->view_width;
                color = src[src_x];
                if (swap_bytes)
                    color = (uint16_t)((color << 8) | (color >> 8));
            }
            dst[x] = color;
        }
    }
}
