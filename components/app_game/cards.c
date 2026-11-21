// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/cards.h"

static uint32_t next_random(cards_t *cards)
{
    uint32_t x = cards->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cards->rng = x ? x : 0x6d2b79f5U;
    return cards->rng;
}

static uint8_t draw_card(cards_t *cards)
{
    return (uint8_t)(1U + next_random(cards) % 13U);
}

void cards_init(cards_t *cards, uint32_t seed)
{
    const uint8_t best = cards->view.best_streak;
    memset(cards, 0, sizeof(*cards));
    cards->rng = seed ? seed : 0x2545f491U;
    cards->view.phase = APP_CARDS_READY;
    cards->view.best_streak = best;
    cards->view.current = draw_card(cards);
}

uint32_t cards_deal(cards_t *cards, uint32_t sparks)
{
    if (cards->view.phase != APP_CARDS_READY || sparks < CARDS_DEAL_COST) {
        return 0;
    }
    cards->view.phase = APP_CARDS_GUESS;
    cards->view.current = draw_card(cards);
    cards->view.next = draw_card(cards);
    cards->view.pot = CARDS_DEAL_COST;
    cards->view.streak = 0;
    cards->view.result = APP_CARD_RESULT_NONE;
    return CARDS_DEAL_COST;
}

bool cards_guess(cards_t *cards, cards_guess_t guess)
{
    if (cards->view.phase != APP_CARDS_GUESS) {
        return false;
    }
    const uint8_t a = cards->view.current;
    const uint8_t b = cards->view.next;
    if (a == b) {
        cards->view.result = APP_CARD_RESULT_TIE;
    } else if ((b > a) == (guess == CARDS_GUESS_HIGHER)) {
        cards->view.result = APP_CARD_RESULT_WIN;
    } else {
        cards->view.result = APP_CARD_RESULT_LOSE;
    }
    cards->view.phase = APP_CARDS_REVEAL;
    cards->reveal_ticks = CARDS_REVEAL_TICKS;
    return true;
}

uint32_t cards_cash_out(cards_t *cards)
{
    if (cards->view.phase != APP_CARDS_GUESS || cards->view.streak == 0) {
        return 0;
    }
    const uint32_t won = cards->view.pot;
    cards->view.pot = 0;
    cards->view.phase = APP_CARDS_READY;
    return won;
}

uint32_t cards_update(cards_t *cards, uint32_t *events)
{
    if (cards->view.phase != APP_CARDS_REVEAL || --cards->reveal_ticks > 0) {
        return 0;
    }
    app_cards_snapshot_t *v = &cards->view;
    if (v->result == APP_CARD_RESULT_LOSE) {
        v->pot = 0;
        v->current = v->next;
        v->phase = APP_CARDS_READY;
        return 0;
    }
    if (v->result == APP_CARD_RESULT_WIN) {
        ++v->streak;
        v->pot = (uint16_t)(v->pot * 2U);
        if (v->streak > v->best_streak) {
            v->best_streak = v->streak;
        }
    }
    v->current = v->next;
    if (v->streak >= CARDS_MAX_STREAK) {
        const uint32_t won = v->pot;
        v->pot = 0;
        v->phase = APP_CARDS_READY;
        *events |= APP_EVENT_CASH_OUT;
        return won;
    }
    v->next = draw_card(cards);
    v->phase = APP_CARDS_GUESS;
    *events |= APP_EVENT_CARD_NEXT;
    return 0;
}
