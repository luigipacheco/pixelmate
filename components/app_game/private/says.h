// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Bitsy Says: watch a growing sequence of four colored pads (Bitsy's eyes
 * flash each color), then repeat it. Private to app_game; spark accounting
 * stays with the caller. */
#include <stdbool.h>
#include <stdint.h>
#include "../include/app_game.h"

typedef struct {
    app_says_snapshot_t view;
    uint8_t seq[APP_SAYS_MAX];
    uint32_t rng;
    uint16_t timer;        /* Ticks into the current step / wait. */
    uint8_t lit_ticks;     /* Remaining glow of a tapped pad. */
} says_t;

void says_init(says_t *s, uint32_t seed);
/** Start a run if affordable; returns sparks to subtract (0 if not started). */
uint32_t says_start(says_t *s, uint32_t sparks, uint8_t *cue);
/** Tap a pad while it is the player's turn. */
void says_tap(says_t *s, int pad, uint8_t *cue);
/** One 30 Hz tick. Returns sparks won when a run ends (payout may be 0);
 * *done is set when a run ended this tick. */
uint32_t says_update(says_t *s, bool *done, uint8_t *cue);
/** Sparks a run with this many completed rounds pays. */
uint32_t says_payout(uint8_t rounds);
