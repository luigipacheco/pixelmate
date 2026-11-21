// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Spark Grid: a 5x5 board of hidden x1/x2/x3 tiles and duds with row/column
 * hints. Private to app_game; spark accounting stays with the caller. */
#include <stdbool.h>
#include <stdint.h>
#include "../include/app_game.h"

enum {
    GRID_DUDS = 6,
    GRID_RESULT_TICKS = 75,   /* Revealed board stays this long. */
};

typedef struct {
    app_grid_snapshot_t view;
    uint32_t rng;
    uint16_t result_ticks;
} grid_t;

void grid_init(grid_t *grid, uint32_t seed);
/** Start a round if affordable; returns sparks to subtract (0 if not started). */
uint32_t grid_deal(grid_t *grid, uint32_t sparks);
/** Flip one hidden tile. Returns sparks won (board cleared) and sets *events. */
uint32_t grid_flip(grid_t *grid, int tile, uint32_t *events);
/** Bank the score if anything was multiplied; returns sparks won. */
uint32_t grid_cash(grid_t *grid, uint32_t *events);
/** Advance the result timer back to READY. */
void grid_update(grid_t *grid);
