// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/bits.h"

#define TICKS_PER_SECOND 30U
enum {
    FIRST_LIMIT_TICKS = 12 * TICKS_PER_SECOND,  /* Time for the first target. */
    MIN_LIMIT_TICKS = 4 * TICKS_PER_SECOND,     /* Floor as the run speeds up. */
    LIMIT_STEP_TICKS = 9,                       /* ~0.3 s less per hit. */
    RESULT_TICKS = 75,
    EASY_HITS = 3,     /* Targets 1..15 (4 bits) for the first hits... */
    MEDIUM_HITS = 6,   /* ...then 1..63 (6 bits), then all 8 bits. */
};

static uint32_t next_random(bits_t *b)
{
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x ? x : 0x85ebca6bU;
    return b->rng;
}

uint32_t bits_payout(uint16_t hits)
{
    /* One spark per hit, capped: 5 hits pay back the entry, a long run
     * tops out at APP_BITS_PAYOUT_MAX so the arcade never outearns walking. */
    return hits < APP_BITS_PAYOUT_MAX ? hits : APP_BITS_PAYOUT_MAX;
}

static void new_target(bits_t *b)
{
    app_bits_snapshot_t *v = &b->view;
    const uint32_t max = v->hits < EASY_HITS ? 15U : (v->hits < MEDIUM_HITS ? 63U : 255U);
    uint8_t target;
    do {
        target = (uint8_t)(1U + next_random(b) % max);
    } while (target == v->target);
    v->target = target;
    v->switches = 0;
    const uint32_t cut = (uint32_t)v->hits * LIMIT_STEP_TICKS;
    v->time_limit = (uint16_t)(FIRST_LIMIT_TICKS > MIN_LIMIT_TICKS + cut
                               ? FIRST_LIMIT_TICKS - cut : MIN_LIMIT_TICKS);
    v->time_left = v->time_limit;
}

void bits_init(bits_t *b, uint32_t seed)
{
    const uint16_t best = b->view.best;
    memset(b, 0, sizeof(*b));
    b->rng = seed ? seed : 0xc2b2ae35U;
    b->view.best = best;
    b->view.phase = APP_BITS_READY;
}

uint32_t bits_start(bits_t *b, uint32_t sparks)
{
    if (b->view.phase == APP_BITS_PLAYING || sparks < APP_BITS_COST) {
        return 0;
    }
    app_bits_snapshot_t *v = &b->view;
    v->hits = 0;
    v->payout = 0;
    v->last_was_hit = false;
    v->phase = APP_BITS_PLAYING;
    new_target(b);
    return APP_BITS_COST;
}

void bits_toggle(bits_t *b, int index, uint32_t *events)
{
    app_bits_snapshot_t *v = &b->view;
    if (v->phase != APP_BITS_PLAYING || index < 0 || index >= APP_BITS) {
        return;
    }
    v->switches ^= (uint8_t)(0x80U >> index);
    v->last_was_hit = v->switches == v->target;
    *events |= APP_EVENT_BIT_FLIP;
    if (v->last_was_hit) {
        ++v->hits;
        new_target(b);
    }
}

uint32_t bits_update(bits_t *b, uint32_t *events)
{
    app_bits_snapshot_t *v = &b->view;
    if (v->phase == APP_BITS_RESULT) {
        if (b->result_ticks && --b->result_ticks == 0) {
            v->phase = APP_BITS_READY;
        }
        return 0;
    }
    if (v->phase != APP_BITS_PLAYING || --v->time_left > 0) {
        return 0;
    }
    v->payout = (uint16_t)bits_payout(v->hits);
    if (v->hits > v->best) {
        v->best = v->hits;
    }
    v->phase = APP_BITS_RESULT;
    b->result_ticks = RESULT_TICKS;
    *events |= APP_EVENT_BIT_DONE;
    return v->payout;
}
