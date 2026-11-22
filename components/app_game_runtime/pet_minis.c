// SPDX-License-Identifier: Apache-2.0
/* Bitsy Says and Spark Stack drawing, plus their cabinet demos and store
 * icons. Bitsy's eyes take the lit Says color in pet_view.c. */
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color WARN = {255, 48, 48, 255};
static const Color PAPER = {240, 240, 232, 255};
static const Color PADS[APP_SAYS_PADS] = {
    {232, 56, 64, 255}, {72, 200, 96, 255}, {64, 120, 240, 255}, {244, 208, 64, 255},
};

Color screens_says_color(int pad)
{
    return pad >= 0 && pad < APP_SAYS_PADS ? PADS[pad] : PAPER;
}

/* ---- Bitsy Says ------------------------------------------------------ */

void screens_draw_says(const draw_ctx_t *d, int ox)
{
    const app_says_snapshot_t *s = &d->snap->says;
    char text[24];
    const bool blink = (d->view->t / 6U) % 2U;
    for (int i = 0; i < APP_SAYS_PADS; ++i) {
        const app_rect_t r = app_says_pad_rect(i);
        const bool lit = s->lit == i;
        const bool wrong = s->phase == APP_SAYS_RESULT && s->failed && s->wrong == i;
        Color c = px_scale(PADS[i], lit ? d->level : 0.22f * d->level);
        if (wrong) {
            c = blink ? px_scale(WARN, d->level) : px_scale(PADS[i], 0.22f * d->level);
        }
        px_box(r.x + ox, r.y, r.w, r.h, 0, c);
        px_box(r.x + ox, r.y, r.w, r.h, 3, lit ? px_scale(PAPER, d->level) : px_scale(PADS[i], 0.5f * d->level));
    }
    switch (s->phase) {
    case APP_SAYS_READY: {
        const bool can = d->snap->sparks >= APP_SAYS_COST;
        snprintf(text, sizeof(text), "PLAY %d", APP_SAYS_COST);
        screens_button(d, app_says_button_rect(), ox, 0, text, 4, can ? d->color : d->dim, can);
        if (s->best) {
            snprintf(text, sizeof(text), "BEST %u", s->best);
            px_text_center(text, 240 + ox, 150, 2, d->accent, false);
        } else {
            px_text_center("WATCH THEN REPEAT", 240 + ox, 150, 2, d->dim, false);
        }
        break;
    }
    case APP_SAYS_SHOW:
        snprintf(text, sizeof(text), "WATCH  ROUND %u", s->round);
        px_text_center(text, 240 + ox, 150, 2, d->color, false);
        break;
    case APP_SAYS_INPUT:
        snprintf(text, sizeof(text), "YOUR TURN  %u/%u", s->entered, s->round);
        px_text_center(text, 240 + ox, 150, 2, d->accent, false);
        break;
    case APP_SAYS_RESULT:
        if (s->failed) {
            snprintf(text, sizeof(text), "%s  +%u", s->wrong >= 0 ? "WRONG!" : "TOO SLOW", s->payout);
        } else {
            snprintf(text, sizeof(text), "PERFECT!  +%u", s->payout);
        }
        px_text_center(text, 240 + ox, 150, 2, s->payout ? d->accent : WARN, false);
        break;
    default:
        break;
    }
}

/* ---- Spark Stack ----------------------------------------------------- */

enum { FALL_TICKS = 14 };

static int row_y(int row)
{
    return APP_STACK_BOTTOM - (row + 1) * APP_STACK_ROW_H;
}

void screens_draw_stack(const draw_ctx_t *d, int ox)
{
    const app_stack_snapshot_t *s = &d->snap->stack;
    char text[24];
    const int x0 = APP_STACK_X + ox;
    const int top = row_y(APP_STACK_ROWS - 1);
    /* Tower frame and the top line to reach. */
    px_box(x0 - 6, top - 6, APP_STACK_COLS * APP_STACK_CELL + 12, APP_STACK_BOTTOM - top + 12, 2, d->dim);
    DrawRectangle(x0, top - 2, APP_STACK_COLS * APP_STACK_CELL, 2, px_scale(d->accent, 0.6f));
    for (int r = 0; r < s->rows; ++r) {
        const float k = 0.45f + 0.55f * (float)(r + 1) / (float)APP_STACK_ROWS;
        px_box(x0 + s->left[r] * APP_STACK_CELL, row_y(r), s->row_w[r] * APP_STACK_CELL, APP_STACK_ROW_H - 2, 0,
               px_scale(d->color, k));
    }
    /* The overhang that just fell off drops out of view. */
    static uint8_t seen_seq;
    static uint32_t fall_start;
    if (s->drop_seq != seen_seq) {
        seen_seq = s->drop_seq;
        fall_start = d->view->t;
    }
    const uint32_t fall = d->view->t - fall_start;
    if (s->cut_w && fall < FALL_TICKS) {
        px_box(x0 + s->cut_left * APP_STACK_CELL, row_y(s->cut_row) + (int)(fall * fall),
               s->cut_w * APP_STACK_CELL, APP_STACK_ROW_H - 2, 0, px_scale(WARN, 0.8f * d->level));
    }
    if (s->phase == APP_STACK_PLAYING) {
        const int bx = x0 + s->pos_q * APP_STACK_CELL / 16;
        px_box(bx, row_y(s->rows), s->width * APP_STACK_CELL, APP_STACK_ROW_H - 2, 0, d->accent);
    }
    switch (s->phase) {
    case APP_STACK_READY: {
        const bool can = d->snap->sparks >= APP_STACK_COST;
        snprintf(text, sizeof(text), "PLAY %d", APP_STACK_COST);
        screens_button(d, app_stack_button_rect(), ox, 0, text, 4, can ? d->color : d->dim, can);
        snprintf(text, sizeof(text), s->best ? "BEST %u ROWS" : "TAP TO DROP", s->best);
        px_text_center(text, 240 + ox, 330, 2, s->best ? d->accent : d->dim, false);
        break;
    }
    case APP_STACK_PLAYING:
        snprintf(text, sizeof(text), "ROWS %u", s->rows);
        px_text(text, 24 + ox, 112, 3, d->color, d->gap);
        if (s->perfects) {
            snprintf(text, sizeof(text), "PERFECT x%u", s->perfects);
            px_text(text, 200 + ox, 118, 2, d->accent, false);
        }
        break;
    case APP_STACK_RESULT:
        snprintf(text, sizeof(text), "%s  +%u", s->topped ? "TOP!" : "MISS", s->payout);
        px_text_center(text, 240 + ox, 112, 3, s->payout ? d->accent : WARN, d->gap);
        break;
    default:
        break;
    }
}

/* ---- Cabinet demos and store icons ---------------------------------- */

void screens_attract_says(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    const int lit = (int)((t / 12U) % APP_SAYS_PADS);
    const int pw = (w - 12) / 2;
    for (int i = 0; i < APP_SAYS_PADS; ++i) {
        const int px = x + 4 + (i % 2) * (pw + 4), py = y + 8 + (i / 2) * 44;
        px_box(px, py, pw, 40, 0, px_scale(PADS[i], i == lit ? d->level : 0.25f * d->level));
    }
}

void screens_attract_stack(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    const int cell = (w - 8) / 12, rows = (int)((t / 10U) % 8U);
    static const uint8_t lefts[8] = {3, 3, 4, 4, 4, 5, 5, 5};
    static const uint8_t widths[8] = {6, 5, 5, 4, 4, 3, 3, 2};
    for (int r = 0; r < rows; ++r) {
        px_box(x + 4 + lefts[r] * cell, y + 84 - r * 10, widths[r] * cell, 8, 0, px_scale(d->color, 0.5f + 0.06f * r));
    }
    const int slide = (int)((t * 2U) % 20U);
    const int sx = slide < 10 ? slide : 20 - slide;
    px_box(x + 4 + sx * cell / 2, y + 84 - rows * 10, widths[rows % 8] * cell, 8, 0, d->accent);
}

void screens_icon_says(const draw_ctx_t *d, int x, int y, Color c)
{
    (void)c;
    for (int i = 0; i < APP_SAYS_PADS; ++i) {
        px_box(x + (i % 2) * 30, y + 2 + (i / 2) * 30, 27, 27, 0, px_scale(PADS[i], d->level));
    }
}

void screens_icon_stack(const draw_ctx_t *d, int x, int y, Color c)
{
    px_box(x + 6, y + 44, 48, 12, 0, c);
    px_box(x + 12, y + 30, 40, 12, 0, c);
    px_box(x + 16, y + 16, 30, 12, 0, c);
    px_box(x + 22, y + 2, 20, 12, 0, d->accent);
}
