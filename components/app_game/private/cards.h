// SPDX-License-Identifier: Apache-2.0
#pragma once
/* High-low card game rules. Private to app_game; spark accounting stays with
 * the caller, which passes the balance in and applies the returned delta. */
#include <stdint.h>
#include "../include/app_game.h"

enum {
    CARDS_DEAL_COST = APP_CARDS_DEAL_COST,
    CARDS_MAX_STREAK = 5,        /* Pot auto-cashes after this many wins. */
    CARDS_REVEAL_TICKS = 36,     /* Time the revealed card stays before resolving. */
};

typedef struct {
    app_cards_snapshot_t view;
    uint32_t rng;
    uint16_t reveal_ticks;
    uint16_t win_pot;            /* Pot after the pending win. */
} cards_t;

typedef enum {
    CARDS_GUESS_HIGHER = 0,
    CARDS_GUESS_LOWER,
} cards_guess_t;

void cards_init(cards_t *cards, uint32_t seed);
/** Start a run if affordable; returns sparks to subtract (0 if not dealt). */
uint32_t cards_deal(cards_t *cards, uint32_t sparks);
/** Lock in a guess and reveal; returns true when accepted. */
bool cards_guess(cards_t *cards, cards_guess_t guess);
/** Stop and bank the pot; returns sparks to add. */
uint32_t cards_cash_out(cards_t *cards);
/** Advance timers. Returns sparks to add (auto cash-out) and sets *events bits. */
uint32_t cards_update(cards_t *cards, uint32_t *events);
