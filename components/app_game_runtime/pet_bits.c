// SPDX-License-Identifier: Apache-2.0
/* Bit Flip drawing: target number, timer bar, eight switches, sum and score. */
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color INK = {12, 12, 12, 255};
static const Color WARN = {255, 48, 48, 255};

enum {
    TARGET_Y = 150,
    TIMER_X = 40, TIMER_Y = 234, TIMER_W = 400, TIMER_H = 12,
    LABEL_Y = 360,
    INFO_Y = 392,
};

/* Payout of a run with this many hits (mirrors the model's formula). */
static unsigned payout_for(unsigned hits)
{
    return hits + hits * hits / 10U;
}

static void draw_timer(const draw_ctx_t *d, const app_bits_snapshot_t *b, int ox)
{
    DrawRectangle(TIMER_X + ox, TIMER_Y, TIMER_W, TIMER_H, px_scale(d->color, 0.15f));
    if (b->phase != APP_BITS_PLAYING || !b->time_limit) {
        return;
    }
    const int w = TIMER_W * b->time_left / b->time_limit;
    const int pct = 100 * b->time_left / b->time_limit;
    Color c = pct > 50 ? d->color : (pct > 25 ? d->accent : WARN);
    if (pct <= 20 && (d->view->t / 4U) % 2U) {
        c = px_scale(WARN, 0.4f);   /* Hurry-up flash. */
    }
    DrawRectangle(TIMER_X + ox, TIMER_Y, w, TIMER_H, c);
}

static void draw_switch(const draw_ctx_t *d, const app_bits_snapshot_t *b, int i, int ox)
{
    const app_rect_t r = app_bits_switch_rect(i);
    const int x = r.x + ox, y = r.y;
    const bool on = b->switches & (0x80U >> i);
    const bool live = b->phase == APP_BITS_PLAYING;
    const bool kicked = d->view->bits_flip_t < 1.0f && (d->view->bits_flip_mask & (0x80U >> i));
    const int lift = kicked ? (int)(6.0f * (1.0f - d->view->bits_flip_t)) : 0;
    /* Housing, then the lever: up and lit for 1, down and dark for 0. */
    px_box(x, y, r.w, r.h, 3, live ? (on ? d->color : d->dim) : px_scale(d->dim, 0.7f));
    const int lever_h = r.h / 2 - 8;
    const int lever_y = on ? y + 6 - lift : y + r.h / 2 + 2 + lift;
    px_box(x + 6, lever_y, r.w - 12, lever_h, 0, on ? d->color : px_scale(d->color, 0.18f));
    px_text_center(on ? "1" : "0", x + r.w / 2, lever_y + lever_h / 2 - 10, 3, on ? INK : d->dim, false);
    char label[4];
    snprintf(label, sizeof(label), "%u", 0x80U >> i);
    px_text_center(label, x + r.w / 2, LABEL_Y, 2, on ? d->accent : d->dim, false);
}

static unsigned popcount_sum(uint8_t v)
{
    return v;   /* The switches' value is the number itself. */
}

void screens_draw_bits(const draw_ctx_t *d, int ox)
{
    const app_bits_snapshot_t *b = &d->snap->bits;
    char text[24];
    px_text_center("TARGET", 240 + ox, TARGET_Y - 18, 2, d->dim, false);
    switch (b->phase) {
    case APP_BITS_PLAYING: {
        /* Pop the number in when a new target arrives. */
        const int scale = d->view->bits_pop_t < 1.0f ? 11 : 10;
        snprintf(text, sizeof(text), "%u", b->target);
        px_text_center(text, 240 + ox, TARGET_Y - (scale - 10) * 3, scale, d->color, d->gap);
        break;
    }
    case APP_BITS_RESULT:
        px_text_center("TIME!", 240 + ox, TARGET_Y + 6, 8, WARN, d->gap);
        break;
    default:
        px_text_center("?", 240 + ox, TARGET_Y, 10, d->dim, d->gap);
        break;
    }
    draw_timer(d, b, ox);
    for (int i = 0; i < APP_BITS; ++i) {
        draw_switch(d, b, i, ox);
    }

    if (b->phase == APP_BITS_PLAYING) {
        snprintf(text, sizeof(text), "SUM %u", popcount_sum(b->switches));
        px_text(text, 24 + ox, INFO_Y, 3, b->switches > b->target ? WARN : d->color, d->gap);
        snprintf(text, sizeof(text), "%u HITS +%u", b->hits, payout_for(b->hits));
        px_text(text, 456 + ox - px_text_width(text, 3), INFO_Y, 3, d->accent, d->gap);
        return;
    }
    if (b->phase == APP_BITS_RESULT) {
        snprintf(text, sizeof(text), "%u HITS  +%u", b->hits, b->payout);
        px_text_center(text, 240 + ox, INFO_Y + 8, 3, b->payout ? d->accent : d->dim, d->gap);
        return;
    }
    const bool can = d->snap->sparks >= APP_BITS_COST;
    snprintf(text, sizeof(text), "PLAY %d", APP_BITS_COST);
    screens_button(d, app_bits_button_rect(), ox, 0, text, 4, can ? d->color : d->dim, can);
    if (b->best) {
        snprintf(text, sizeof(text), "BEST %u", b->best);
        px_text_center(text, 240 + ox, TIMER_Y - 2, 2, d->accent, false);
    }
}
