// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/grid.h"

/* Multiplier mixes (x2 count, x3 count). Clearing a board pays 2^a * 3^b:
 * 24..96 sparks for a 5-spark round, so careful play is clearly rewarded. */
static const uint8_t s_mixes[][2] = {
    {3, 1}, {0, 3}, {5, 0}, {2, 2}, {4, 1}, {1, 3}, {3, 2}, {5, 1},
};
#define MIX_COUNT (sizeof(s_mixes) / sizeof(s_mixes[0]))
#define ALL_TILES ((1UL << APP_GRID_TILES) - 1UL)

static uint32_t next_random(grid_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x ? x : 0x3c6ef372U;
    return g->rng;
}

/* Put `value` on `count` random tiles that still hold 1. */
static void scatter(grid_t *g, uint8_t value, int count)
{
    while (count > 0) {
        const int tile = (int)(next_random(g) % APP_GRID_TILES);
        if (g->view.tiles[tile] == 1) {
            g->view.tiles[tile] = value;
            --count;
        }
    }
}

static void compute_hints(app_grid_snapshot_t *v)
{
    memset(v->row_sum, 0, sizeof(v->row_sum));
    memset(v->row_duds, 0, sizeof(v->row_duds));
    memset(v->col_sum, 0, sizeof(v->col_sum));
    memset(v->col_duds, 0, sizeof(v->col_duds));
    for (int i = 0; i < APP_GRID_TILES; ++i) {
        const int row = i / APP_GRID_N, col = i % APP_GRID_N;
        v->row_sum[row] = (uint8_t)(v->row_sum[row] + v->tiles[i]);
        v->col_sum[col] = (uint8_t)(v->col_sum[col] + v->tiles[i]);
        if (v->tiles[i] == APP_GRID_DUD) {
            ++v->row_duds[row];
            ++v->col_duds[col];
        }
    }
}

void grid_init(grid_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x9e3779b1U;
    g->view.phase = APP_GRID_READY;
    g->view.last_flip = -1;
}

uint32_t grid_deal(grid_t *g, uint32_t sparks)
{
    if (g->view.phase == APP_GRID_PLAYING || sparks < APP_GRID_COST) {
        return 0;
    }
    app_grid_snapshot_t *v = &g->view;
    memset(v->tiles, 1, sizeof(v->tiles));
    const uint8_t *mix = s_mixes[next_random(g) % MIX_COUNT];
    scatter(g, APP_GRID_DUD, GRID_DUDS);
    scatter(g, 2, mix[0]);
    scatter(g, 3, mix[1]);
    compute_hints(v);
    v->revealed = 0;
    v->score = 1;
    v->payout = 0;
    v->last_flip = -1;
    v->result = APP_GRID_RESULT_NONE;
    v->phase = APP_GRID_PLAYING;
    g->result_ticks = 0;
    return APP_GRID_COST;
}

static void finish(grid_t *g, app_grid_result_t result, uint32_t payout, uint32_t *events)
{
    g->view.result = result;
    g->view.payout = (uint16_t)payout;
    g->view.revealed = ALL_TILES;
    g->view.phase = APP_GRID_RESULT;
    g->result_ticks = GRID_RESULT_TICKS;
    *events |= APP_EVENT_GRID_DONE;
}

static bool cleared(const app_grid_snapshot_t *v)
{
    for (int i = 0; i < APP_GRID_TILES; ++i) {
        if (v->tiles[i] > 1 && !(v->revealed & (1UL << i))) {
            return false;
        }
    }
    return true;
}

uint32_t grid_flip(grid_t *g, int tile, uint32_t *events)
{
    app_grid_snapshot_t *v = &g->view;
    if (v->phase != APP_GRID_PLAYING || tile < 0 || tile >= APP_GRID_TILES
            || (v->revealed & (1UL << tile))) {
        return 0;
    }
    v->revealed |= 1UL << tile;
    v->last_flip = (int8_t)tile;
    *events |= APP_EVENT_GRID_FLIP;
    if (v->tiles[tile] == APP_GRID_DUD) {
        v->score = 0;
        finish(g, APP_GRID_RESULT_DUD, 0, events);
        return 0;
    }
    v->score = (uint16_t)(v->score * v->tiles[tile]);
    if (cleared(v)) {
        const uint32_t won = v->score;
        finish(g, APP_GRID_RESULT_CLEARED, won, events);
        return won;
    }
    return 0;
}

uint32_t grid_cash(grid_t *g, uint32_t *events)
{
    app_grid_snapshot_t *v = &g->view;
    if (v->phase != APP_GRID_PLAYING || v->score <= 1) {
        return 0;
    }
    const uint32_t won = v->score;
    finish(g, APP_GRID_RESULT_CASHED, won, events);
    return won;
}

void grid_update(grid_t *g)
{
    if (g->view.phase == APP_GRID_RESULT && g->result_ticks && --g->result_ticks == 0) {
        g->view.phase = APP_GRID_READY;
    }
}
