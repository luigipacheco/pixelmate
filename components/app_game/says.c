// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/says.h"

enum {
    START_WAIT = 18,      /* Ticks before the first color. */
    ROUND_WAIT = 22,      /* Pause after a round before the longer sequence. */
    GAP_TICKS = 6,        /* Dark gap between shown colors. */
    TAP_GLOW = 8,         /* A tapped pad glows this long. */
    INPUT_TIMEOUT = 150,  /* 5 s per tap, or the run ends. */
    RESULT_TICKS = 75,
};

static uint32_t next_random(says_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x ? x : 0x2c1b3c6dU;
    return s->rng;
}

/* Each color shows a little faster as the sequence grows. */
static uint16_t on_ticks(uint8_t round)
{
    return (uint16_t)(round < 10 ? 18 - round : 8);
}

uint32_t says_payout(uint8_t rounds)
{
    return rounds < APP_SAYS_PAYOUT_MAX ? rounds : APP_SAYS_PAYOUT_MAX;
}

void says_init(says_t *s, uint32_t seed)
{
    const uint8_t best = s->view.best;
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 0x7f4a7c15U;
    s->view.phase = APP_SAYS_READY;
    s->view.lit = -1;
    s->view.best = best;
}

static void begin_show(says_t *s, uint16_t wait)
{
    s->view.phase = APP_SAYS_SHOW;
    s->view.shown = 0;
    s->view.lit = -1;
    s->view.entered = 0;
    s->timer = 0;
    s->view.wait = wait;
}

uint32_t says_start(says_t *s, uint32_t sparks, uint8_t *cue)
{
    if (s->view.phase != APP_SAYS_READY || sparks < APP_SAYS_COST) {
        return 0;
    }
    for (int i = 0; i < APP_SAYS_MAX; ++i) {
        s->seq[i] = (uint8_t)(next_random(s) % APP_SAYS_PADS);
    }
    s->view.round = 1;
    s->view.payout = 0;
    s->view.failed = false;
    begin_show(s, START_WAIT);
    *cue = APP_CUE_START;
    return APP_SAYS_COST;
}

static void finish(says_t *s, uint8_t rounds_done, bool failed, uint8_t *cue)
{
    s->view.phase = APP_SAYS_RESULT;
    s->view.failed = failed;
    s->view.payout = (uint8_t)says_payout(rounds_done);
    if (rounds_done > s->view.best) {
        s->view.best = rounds_done;
    }
    s->timer = 0;
    *cue = s->view.payout >= 2 * APP_SAYS_COST ? APP_CUE_WIN : (s->view.payout ? APP_CUE_COINS : APP_CUE_LOSE);
}

void says_tap(says_t *s, int pad, uint8_t *cue)
{
    app_says_snapshot_t *v = &s->view;
    if (v->phase != APP_SAYS_INPUT || pad < 0 || pad >= APP_SAYS_PADS) {
        return;
    }
    v->lit = (int8_t)pad;
    s->lit_ticks = TAP_GLOW;
    s->timer = 0;
    if (s->seq[v->entered] != pad) {
        v->wrong = (int8_t)pad;
        finish(s, (uint8_t)(v->round - 1), true, cue);
        return;
    }
    *cue = (uint8_t)(APP_CUE_PAD0 + pad);
    if (++v->entered < v->round) {
        return;
    }
    if (v->round >= APP_SAYS_MAX) {
        finish(s, v->round, false, cue);
        return;
    }
    ++v->round;
    begin_show(s, ROUND_WAIT);
}

uint32_t says_update(says_t *s, bool *done, uint8_t *cue)
{
    app_says_snapshot_t *v = &s->view;
    *done = false;
    switch (v->phase) {
    case APP_SAYS_SHOW: {
        if (v->wait) {
            --v->wait;
            if (s->lit_ticks && --s->lit_ticks == 0) {
                v->lit = -1;
            }
            return 0;
        }
        const uint16_t on = on_ticks(v->round);
        if (s->timer == 0) {
            v->lit = (int8_t)s->seq[v->shown];
            *cue = (uint8_t)(APP_CUE_PAD0 + v->lit);
        } else if (s->timer == on) {
            v->lit = -1;
        }
        if (++s->timer >= on + GAP_TICKS) {
            s->timer = 0;
            if (++v->shown >= v->round) {
                v->phase = APP_SAYS_INPUT;   /* Your turn. */
                v->entered = 0;
                v->wrong = -1;
            }
        }
        return 0;
    }
    case APP_SAYS_INPUT:
        if (s->lit_ticks && --s->lit_ticks == 0) {
            v->lit = -1;
        }
        if (++s->timer >= INPUT_TIMEOUT) {
            v->wrong = -1;
            finish(s, (uint8_t)(v->round - 1), true, cue);   /* Paid on the first RESULT tick. */
        }
        return 0;
    case APP_SAYS_RESULT:
        if (s->timer == 0) {
            s->timer = 1;
            *done = true;   /* Report the payout once, on the first result tick. */
            return v->payout;
        }
        if (s->lit_ticks && --s->lit_ticks == 0) {
            v->lit = -1;
        }
        if (++s->timer >= RESULT_TICKS) {
            v->phase = APP_SAYS_READY;
            v->lit = -1;
        }
        return 0;
    default:
        return 0;
    }
}
