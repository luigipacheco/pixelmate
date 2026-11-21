// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/slots.h"

/* One strip shared by all reels (each starts at a different offset).
 * Rarer symbols pay more: 1 seven, 1 star, 2 eyes, 3 bolts, 5 hearts, 4 shoes. */
static const uint8_t s_strip[APP_SLOT_STRIP] = {
    APP_SLOT_SHOE, APP_SLOT_HEART, APP_SLOT_BOLT, APP_SLOT_SHOE,
    APP_SLOT_EYE, APP_SLOT_HEART, APP_SLOT_STAR, APP_SLOT_SHOE,
    APP_SLOT_BOLT, APP_SLOT_HEART, APP_SLOT_SEVEN, APP_SLOT_SHOE,
    APP_SLOT_HEART, APP_SLOT_BOLT, APP_SLOT_EYE, APP_SLOT_HEART,
};

/* Three-of-a-kind payouts in sparks, indexed by app_slot_symbol_t. */
static const uint16_t s_triple_pay[APP_SLOT_SYMBOLS] = {150, 60, 30, 20, 10, 8};
/* A pair refunds the bet. Blind play returns ~72%; aiming the stops does better,
 * but the machine stays a spark sink so walking remains the main income. */
enum { PAIR_PAY = APP_SLOTS_BET, TOTAL_Q = APP_SLOT_STRIP * APP_SLOT_Q };

static uint32_t next_random(slots_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x ? x : 0x1b873593U;
    return s->rng;
}

uint8_t app_game_slot_symbol(int reel_pos_q, int row)
{
    const int index = reel_pos_q / APP_SLOT_Q + row;
    return s_strip[((index % APP_SLOT_STRIP) + APP_SLOT_STRIP) % APP_SLOT_STRIP];
}

uint16_t app_game_slot_triple_pay(int symbol)
{
    return symbol >= 0 && symbol < APP_SLOT_SYMBOLS ? s_triple_pay[symbol] : 0;
}

void slots_init(slots_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 0x68e31da4U;
    for (int i = 0; i < APP_SLOT_REELS; ++i) {
        s->view.pos_q[i] = (uint16_t)((i * 5 % APP_SLOT_STRIP) * APP_SLOT_Q);
    }
    s->view.phase = APP_SLOTS_READY;
}

uint32_t slots_spin(slots_t *s, uint32_t sparks)
{
    if (s->view.phase == APP_SLOTS_SPINNING || sparks < APP_SLOTS_BET) {
        return 0;
    }
    s->view.phase = APP_SLOTS_SPINNING;
    s->view.stopped = 0;
    s->view.payout = 0;
    s->stopping = 0;
    s->result_ticks = 0;
    return APP_SLOTS_BET;
}

bool slots_stop(slots_t *s)
{
    if (s->view.phase != APP_SLOTS_SPINNING) {
        return false;
    }
    for (int i = 0; i < APP_SLOT_REELS; ++i) {
        const uint8_t bit = (uint8_t)(1U << i);
        if (s->view.stopped & bit || s->stopping & bit) {
            continue;
        }
        /* Finish the current symbol, then slip 0..SLOTS_MAX_SLIP more. */
        const uint16_t to_boundary = (uint16_t)((APP_SLOT_Q - s->view.pos_q[i] % APP_SLOT_Q) % APP_SLOT_Q);
        const uint16_t slip = (uint16_t)(next_random(s) % (SLOTS_MAX_SLIP + 1U));
        s->remaining_q[i] = (uint16_t)(to_boundary + slip * APP_SLOT_Q);
        s->stopping |= bit;
        return true;
    }
    return false;
}

static uint32_t evaluate(slots_t *s)
{
    uint8_t sym[APP_SLOT_REELS];
    for (int i = 0; i < APP_SLOT_REELS; ++i) {
        sym[i] = app_game_slot_symbol(s->view.pos_q[i], 0);
    }
    if (sym[0] == sym[1] && sym[1] == sym[2]) {
        return s_triple_pay[sym[0]];
    }
    if (sym[0] == sym[1] || sym[1] == sym[2] || sym[0] == sym[2]) {
        return PAIR_PAY;
    }
    return 0;
}

uint32_t slots_update(slots_t *s, uint32_t *events)
{
    app_slots_snapshot_t *v = &s->view;
    if (v->phase == APP_SLOTS_RESULT) {
        if (s->result_ticks && --s->result_ticks == 0) {
            v->phase = APP_SLOTS_READY;
        }
        return 0;
    }
    if (v->phase != APP_SLOTS_SPINNING) {
        return 0;
    }
    for (int i = 0; i < APP_SLOT_REELS; ++i) {
        const uint8_t bit = (uint8_t)(1U << i);
        if (v->stopped & bit) {
            continue;
        }
        uint16_t step = SLOTS_SPEED_Q;
        if (s->stopping & bit) {
            step = s->remaining_q[i] < step ? s->remaining_q[i] : step;
            s->remaining_q[i] = (uint16_t)(s->remaining_q[i] - step);
            if (s->remaining_q[i] == 0) {
                v->stopped |= bit;
                *events |= APP_EVENT_SLOT_STOP;
            }
        }
        v->pos_q[i] = (uint16_t)((v->pos_q[i] + step) % TOTAL_Q);
    }
    if (v->stopped != (1U << APP_SLOT_REELS) - 1U) {
        return 0;
    }
    v->payout = (uint16_t)evaluate(s);
    v->phase = APP_SLOTS_RESULT;
    s->result_ticks = SLOTS_RESULT_TICKS;
    *events |= APP_EVENT_SLOT_DONE;
    return v->payout;
}
