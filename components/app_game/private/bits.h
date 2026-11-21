// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Bit Flip: set 8 switches (128..1) to a target number before the timer runs
 * out; each hit brings a new target and a shorter timer. Private to app_game;
 * spark accounting stays with the caller. */
#include <stdint.h>
#include "../include/app_game.h"

typedef struct {
    app_bits_snapshot_t view;
    uint32_t rng;
    uint16_t result_ticks;
} bits_t;

void bits_init(bits_t *bits, uint32_t seed);
/** Start a run if affordable; returns sparks to subtract (0 if not started). */
uint32_t bits_start(bits_t *bits, uint32_t sparks);
/** Flip switch `index` (0 = the 128 switch). Sets APP_EVENT_BIT_FLIP. */
void bits_toggle(bits_t *bits, int index, uint32_t *events);
/** Advance the timer. Returns sparks won when the run ends; sets *events. */
uint32_t bits_update(bits_t *bits, uint32_t *events);
/** Payout in sparks for a run with `hits` correct answers. */
uint32_t bits_payout(uint16_t hits);
