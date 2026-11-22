// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/stack.h"

enum {
    Q = 16,               /* Bar position in 1/16 cells. */
    TOP_BONUS = 10,
    RESULT_TICKS = 75,
};

static uint32_t next_random(spark_stack_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x ? x : 0x4f1bbcdcU;
    return s->rng;
}

uint32_t stack_payout(uint8_t rows, uint8_t perfects, bool topped)
{
    const uint32_t p = (uint32_t)rows + perfects + (topped ? TOP_BONUS : 0U);
    return p < APP_STACK_PAYOUT_MAX ? p : APP_STACK_PAYOUT_MAX;
}

void stack_init(spark_stack_t *s, uint32_t seed)
{
    const uint8_t best = s->view.best;
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 0x1b873593U;
    s->view.phase = APP_STACK_READY;
    s->view.best = best;
}

/* The bar for the next row enters from a random side, a bit faster each row. */
static void next_bar(spark_stack_t *s)
{
    app_stack_snapshot_t *v = &s->view;
    v->speed_q = (uint8_t)(3 + v->rows / 2);
    if (next_random(s) & 1U) {
        v->pos_q = 0;
        v->dir = 1;
    } else {
        v->pos_q = (int16_t)((APP_STACK_COLS - v->width) * Q);
        v->dir = -1;
    }
}

uint32_t stack_start(spark_stack_t *s, uint32_t sparks, uint8_t *cue)
{
    app_stack_snapshot_t *v = &s->view;
    if (v->phase != APP_STACK_READY || sparks < APP_STACK_COST) {
        return 0;
    }
    v->rows = 0;
    v->perfects = 0;
    v->payout = 0;
    v->topped = false;
    v->cut_w = 0;
    v->width = APP_STACK_START_W;
    v->phase = APP_STACK_PLAYING;
    next_bar(s);
    *cue = APP_CUE_START;
    return APP_STACK_COST;
}

static void finish(spark_stack_t *s, uint8_t *cue)
{
    app_stack_snapshot_t *v = &s->view;
    v->phase = APP_STACK_RESULT;
    v->payout = (uint8_t)stack_payout(v->rows, v->perfects, v->topped);
    if (v->rows > v->best) {
        v->best = v->rows;
    }
    s->timer = 0;
    *cue = v->topped || v->payout >= 2 * APP_STACK_COST ? APP_CUE_WIN : (v->payout ? APP_CUE_COINS : APP_CUE_LOSE);
}

void stack_drop(spark_stack_t *s, uint8_t *cue)
{
    app_stack_snapshot_t *v = &s->view;
    if (v->phase != APP_STACK_PLAYING) {
        return;
    }
    const int left = (v->pos_q + Q / 2) / Q;   /* Snap to the nearest cell. */
    int l = left, r = left + v->width;
    if (v->rows > 0) {
        const int pl = v->left[v->rows - 1], pr = pl + v->row_w[v->rows - 1];
        l = left > pl ? left : pl;
        r = left + v->width < pr ? left + v->width : pr;
    }
    ++v->drop_seq;
    if (r <= l) {                                /* Missed the tower. */
        v->cut_left = (uint8_t)left;
        v->cut_w = v->width;
        v->cut_row = v->rows;
        finish(s, cue);
        return;
    }
    /* The overhang falls off (the view animates it). */
    v->cut_w = (uint8_t)(v->width - (r - l));
    v->cut_left = (uint8_t)(left < l ? left : r);
    v->cut_row = v->rows;
    const bool perfect = v->cut_w == 0;
    v->left[v->rows] = (uint8_t)l;
    v->row_w[v->rows] = (uint8_t)(r - l);
    v->width = (uint8_t)(r - l);
    ++v->rows;
    if (perfect && v->rows > 1) {
        ++v->perfects;
    }
    *cue = perfect && v->rows > 1 ? APP_CUE_PERFECT : APP_CUE_DROP;
    if (v->rows >= APP_STACK_ROWS) {
        v->topped = true;
        finish(s, cue);
        return;
    }
    next_bar(s);
}

uint32_t stack_update(spark_stack_t *s, bool *done, uint8_t *cue)
{
    app_stack_snapshot_t *v = &s->view;
    (void)cue;
    *done = false;
    if (v->phase == APP_STACK_PLAYING) {
        const int max = (APP_STACK_COLS - v->width) * Q;
        int pos = v->pos_q + v->dir * v->speed_q;
        if (pos <= 0) {
            pos = 0;
            v->dir = 1;
        } else if (pos >= max) {
            pos = max;
            v->dir = -1;
        }
        v->pos_q = (int16_t)pos;
        return 0;
    }
    if (v->phase == APP_STACK_RESULT) {
        if (s->timer++ == 0) {
            *done = true;
            return v->payout;
        }
        if (s->timer >= RESULT_TICKS) {
            v->phase = APP_STACK_READY;
        }
    }
    return 0;
}
