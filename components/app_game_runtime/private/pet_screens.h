// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Per-screen and per-panel content drawing (everything except the eyes,
 * particles and chrome). Private to app_game_runtime; called by pet_view. */
#include "pet_view.h"

typedef struct {
    const pet_view_t *view;
    const app_game_snapshot_t *snap;
    px_grid_t grid;
    bool gap;
    Color color;     /* Chosen palette color. */
    Color accent;    /* Contrasting secondary color. */
    Color dim;       /* Inactive tracks and outlines. */
    float level;     /* 1 = awake, lower while the screen is dimmed. */
} draw_ctx_t;

/** Draw one swipeable screen's content shifted horizontally by ox pixels. */
void screens_draw(const draw_ctx_t *ctx, app_screen_t screen, int ox);
/** Draw a pull-down panel shifted vertically by oy pixels (<= 0 while sliding). */
void screens_draw_overlay(const draw_ctx_t *ctx, app_overlay_t overlay, int oy);
/** Panel height in pixels (menu is partial, style/store cover the screen). */
int screens_overlay_height(app_overlay_t overlay);
/** Cheapest item not owned yet, or NULL when everything is owned. */
const app_store_item_t *screens_next_unlock(const app_game_snapshot_t *snap);
/** Arcade lobby: the cabinet row (pet_arcade.c). */
void screens_draw_arcade(const draw_ctx_t *ctx, int ox);
/** Bitsy Says and Spark Stack (pet_minis.c), with their cabinet demos and store icons. */
void screens_draw_says(const draw_ctx_t *ctx, int ox);
void screens_draw_stack(const draw_ctx_t *ctx, int ox);
void screens_attract_says(const draw_ctx_t *ctx, int x, int y, int w, uint32_t t);
void screens_attract_stack(const draw_ctx_t *ctx, int x, int y, int w, uint32_t t);
void screens_icon_says(const draw_ctx_t *ctx, int x, int y, Color c);
void screens_icon_stack(const draw_ctx_t *ctx, int x, int y, Color c);
/** Bitsy Says pad color (shared with the eyes). */
Color screens_says_color(int pad);
/** TOOLS page (pet_tools.c). */
void screens_draw_tools(const draw_ctx_t *ctx, int ox);
/** Clock mode face (pet_clock.c). */
void screens_draw_clock(const draw_ctx_t *ctx, const pet_clock_t *clock);
/** Bit Flip game (pet_bits.c). */
void screens_draw_bits(const draw_ctx_t *ctx, int ox);
/** Spark Grid game (pet_grid.c). */
void screens_draw_grid(const draw_ctx_t *ctx, int ox);
/** Photo booth page (pet_booth.c), its flash (drawn under the eyes) and store art. */
void screens_draw_booth(const draw_ctx_t *ctx, int ox);
void screens_draw_booth_flash(const draw_ctx_t *ctx);
void screens_camera_icon(const draw_ctx_t *ctx, int x, int y, int w, Color c);
void screens_filter_swatch(const draw_ctx_t *ctx, int filter, int x, int y, bool owned);
void screens_lens_swatch(const draw_ctx_t *ctx, int res, int x, int y, bool owned);
void screens_film_swatch(const draw_ctx_t *ctx, int x, int y, bool owned);
void screens_webcam_swatch(const draw_ctx_t *ctx, int x, int y, bool owned);
/** EXIT button shown while a cabinet's game is open. */
void screens_draw_exit(const draw_ctx_t *ctx, int ox);
/** Labeled button; filled buttons use dark text. */
void screens_button(const draw_ctx_t *ctx, app_rect_t r, int ox, int oy, const char *label,
                    int scale, Color color, bool filled);
/** Draw a slot symbol centered at (cx, cy); scale_pct 100 = reel size. */
void screens_slot_symbol(const draw_ctx_t *ctx, uint8_t symbol, int cx, int cy, int scale_pct);
/** STARVING / HUNGRY / PECKISH / FULL for an energy level 0..100. */
const char *screens_energy_word(uint8_t energy);
/** Card label for 1..13 ("A", "2".."10", "J", "Q", "K"). */
const char *screens_card_label(uint8_t value);
float screens_ease_out(float t);
