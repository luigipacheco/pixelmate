// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Three-reel slot machine with player-stopped reels. Private to app_game;
 * spark accounting stays with the caller (bet in, payout out). */
#include <stdbool.h>
#include <stdint.h>
#include "../include/app_game.h"

enum {
    SLOTS_RESULT_TICKS = 45,   /* Time the result stays before the next spin. */
    SLOTS_SPEED_Q = 6,         /* Reel speed in 1/APP_SLOT_Q symbol per tick. */
    SLOTS_MAX_SLIP = 2,        /* Extra symbols a stopping reel may travel. */
};

typedef struct {
    app_slots_snapshot_t view;
    uint16_t remaining_q[APP_SLOT_REELS];   /* Travel left for stopping reels. */
    uint8_t stopping;                       /* Bit per reel. */
    uint16_t result_ticks;
    uint32_t rng;
} slots_t;

void slots_init(slots_t *slots, uint32_t seed);
/** Start a spin if affordable; returns sparks to subtract (0 if not started). */
uint32_t slots_spin(slots_t *slots, uint32_t sparks);
/** Stop the leftmost still-spinning reel; returns true when accepted. */
bool slots_stop(slots_t *slots);
/** Advance reels/timers. Returns sparks won when a spin resolves; sets *events. */
uint32_t slots_update(slots_t *slots, uint32_t *events);
