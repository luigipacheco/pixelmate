// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Per-screen content drawing (everything except the eyes, overlays and chrome).
 * Private to app_game_runtime; called by pet_view during render. */
#include "pet_view.h"

typedef struct {
    const pet_view_t *view;
    const app_game_snapshot_t *snap;
    px_grid_t grid;
    bool gap;
    Color color;     /* Chosen palette color. */
    Color accent;    /* Contrasting secondary color. */
    Color dim;       /* Inactive tracks and outlines. */
} draw_ctx_t;

/** Draw one screen's content shifted horizontally by ox pixels. */
void screens_draw(const draw_ctx_t *ctx, app_screen_t screen, int ox);
/** Card label for 1..13 ("A", "2".."10", "J", "Q", "K"). */
const char *screens_card_label(uint8_t value);
float screens_ease_out(float t);
