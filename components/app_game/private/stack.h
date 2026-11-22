// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Spark Stack: a bar slides back and forth; tap to drop it on the tower.
 * Whatever hangs over the row below is cut off; miss completely and the
 * run ends. Private to app_game; spark accounting stays with the caller. */
#include <stdbool.h>
#include <stdint.h>
#include "../include/app_game.h"

typedef struct {
    app_stack_snapshot_t view;
    uint32_t rng;
    uint16_t timer;
} spark_stack_t;

void stack_init(spark_stack_t *s, uint32_t seed);
/** Start a run if affordable; returns sparks to subtract (0 if not started). */
uint32_t stack_start(spark_stack_t *s, uint32_t sparks, uint8_t *cue);
/** Tap: drop the moving bar. */
void stack_drop(spark_stack_t *s, uint8_t *cue);
/** One 30 Hz tick. Returns sparks won when a run ends (*done set then). */
uint32_t stack_update(spark_stack_t *s, bool *done, uint8_t *cue);
/** Sparks for a run: one per row, one per perfect drop, a bonus for the top. */
uint32_t stack_payout(uint8_t rows, uint8_t perfects, bool topped);
