// SPDX-License-Identifier: Apache-2.0
/* Spark Grid drawing: tiles, row/column hints, PLAY/CASH and the score. */
#include <math.h>
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color INK = {12, 12, 12, 255};
static const Color WARN = {255, 48, 48, 255};
static const Color TILE_FACE = {236, 236, 226, 255};

enum { TILE_INSET = 3, HINT_GAP = 6 };

static void draw_dud_mark(int cx, int cy, int s, Color c)
{
    /* A small X, drawn as two diagonals of blocks. */
    for (int i = -2; i <= 2; ++i) {
        DrawRectangle(cx + i * s - s / 2, cy + i * s - s / 2, s, s, c);
        DrawRectangle(cx - i * s - s / 2, cy + i * s - s / 2, s, s, c);
    }
}

static void draw_tile(const draw_ctx_t *d, int tile, int ox)
{
    const app_grid_snapshot_t *g = &d->snap->grid;
    const app_rect_t r = app_grid_tile_rect(tile);
    const bool revealed = g->phase != APP_GRID_READY && (g->revealed & (1UL << tile));
    const bool flipping = tile == g->last_flip && d->view->grid_flip_t < 1.0f;
    float width = 1.0f;
    bool face = revealed;
    if (flipping) {
        width = fabsf(cosf(d->view->grid_flip_t * 3.14159f));
        face = d->view->grid_flip_t >= 0.5f;
    }
    const int full = r.w - 2 * TILE_INSET;
    const int w = (int)(full * width);
    if (w < 3) {
        return;
    }
    const int x = r.x + ox + TILE_INSET + (full - w) / 2, y = r.y + TILE_INSET, h = r.h - 2 * TILE_INSET;
    if (!face) {
        px_box(x, y, w, h, 0, px_scale(d->color, 0.18f));
        px_box(x, y, w, h, 2, g->phase == APP_GRID_PLAYING ? d->color : d->dim);
        return;
    }
    /* After a round, tiles nobody flipped are shown dimmer. */
    const bool missed = g->phase == APP_GRID_RESULT && tile != g->last_flip
                        && !(d->view->grid_flipped & (1UL << tile));
    const uint8_t value = g->tiles[tile];
    if (value == APP_GRID_DUD) {
        px_box(x, y, w, h, 0, missed ? px_scale(WARN, 0.35f) : WARN);
        if (width > 0.6f) {
            draw_dud_mark(x + w / 2, y + h / 2, 5, INK);
        }
        return;
    }
    px_box(x, y, w, h, 0, missed ? px_scale(TILE_FACE, 0.35f) : TILE_FACE);
    if (width > 0.6f) {
        char label[4];
        snprintf(label, sizeof(label), "%u", value);
        const Color c = value == 1 ? px_scale(INK, 1.0f) : (value == 3 ? d->accent : px_scale(d->color, 0.55f));
        px_text_center(label, x + w / 2, y + h / 2 - 14, 4, value == 1 ? INK : c, false);
    }
}

static void draw_hint(const draw_ctx_t *d, int x, int y, int w, int h, uint8_t sum, uint8_t duds, bool known)
{
    px_box(x, y, w, h, 2, d->dim);
    if (!known) {
        return;
    }
    char text[6];
    snprintf(text, sizeof(text), "%u", sum);
    px_text_center(text, x + w / 2, y + 6, 2, d->color, false);
    draw_dud_mark(x + 14, y + h - 14, 2, duds ? WARN : d->dim);
    snprintf(text, sizeof(text), "%u", duds);
    px_text(text, x + 26, y + h - 20, 2, duds ? WARN : d->dim, false);
}

static void draw_side(const draw_ctx_t *d, int ox)
{
    const app_game_snapshot_t *s = d->snap;
    const app_grid_snapshot_t *g = &s->grid;
    const app_rect_t b = app_grid_button_rect();
    char text[16];
    if (g->phase == APP_GRID_PLAYING) {
        const bool can_cash = g->score > 1;
        screens_button(d, b, ox, 0, "CASH", 3, can_cash ? d->color : d->dim, can_cash);
    } else {
        const bool can = s->sparks >= APP_GRID_COST && g->phase == APP_GRID_READY;
        snprintf(text, sizeof(text), "PLAY %d", APP_GRID_COST);
        screens_button(d, b, ox, 0, text, 2, can ? d->color : d->dim, can);
    }
    const int cx = b.x + ox + b.w / 2;
    px_text_center("SCORE", cx, 206, 2, d->dim, false);
    snprintf(text, sizeof(text), "x%u", g->phase == APP_GRID_READY ? 0U : g->score);
    px_text_center(text, cx, 228, 3, g->score > 1 ? d->accent : d->dim, d->gap);
    if (g->phase != APP_GRID_RESULT) {
        return;
    }
    switch (g->result) {
    case APP_GRID_RESULT_DUD: px_text_center("DUD!", cx, 268, 3, WARN, d->gap); break;
    case APP_GRID_RESULT_CLEARED: px_text_center("CLEAR", cx, 268, 2, d->accent, false); break;
    default: break;
    }
    if (g->payout) {
        snprintf(text, sizeof(text), "+%u", g->payout);
        px_text_center(text, cx, 290, 3, d->accent, d->gap);
    }
}

void screens_draw_grid(const draw_ctx_t *d, int ox)
{
    const app_grid_snapshot_t *g = &d->snap->grid;
    const bool known = g->phase != APP_GRID_READY;
    for (int i = 0; i < APP_GRID_TILES; ++i) {
        draw_tile(d, i, ox);
    }
    /* Row hints to the right, column hints underneath. */
    const int hint_x = APP_GRID_X + APP_GRID_N * APP_GRID_CELL + HINT_GAP + ox;
    const int hint_y = APP_GRID_Y + APP_GRID_N * APP_GRID_CELL + HINT_GAP;
    for (int i = 0; i < APP_GRID_N; ++i) {
        draw_hint(d, hint_x, APP_GRID_Y + i * APP_GRID_CELL + TILE_INSET, APP_GRID_CELL - 8,
                  APP_GRID_CELL - 2 * TILE_INSET, g->row_sum[i], g->row_duds[i], known);
        draw_hint(d, APP_GRID_X + i * APP_GRID_CELL + TILE_INSET + ox, hint_y, APP_GRID_CELL - 2 * TILE_INSET,
                  APP_GRID_CELL - 8, g->col_sum[i], g->col_duds[i], known);
    }
    draw_side(d, ox);
}
