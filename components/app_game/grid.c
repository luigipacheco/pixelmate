// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/grid.h"

/* Boards per level, as in the pocket-monster game this is modeled on:
 * {x2 tiles, x3 tiles, duds}. A cleared board pays 2^a * 3^b sparks: about
 * 24..54 at level 1 up to 384..576 at level 5, for 10 sparks a round. */
static const uint8_t s_levels[APP_GRID_LEVELS][5][3] = {
    {{3, 1, 6}, {0, 3, 6}, {5, 0, 6}, {2, 2, 6}, {4, 1, 6}},
    {{1, 3, 7}, {6, 0, 7}, {3, 2, 7}, {0, 4, 7}, {5, 1, 7}},
    {{2, 3, 8}, {7, 0, 8}, {4, 2, 8}, {1, 4, 8}, {6, 1, 8}},
    {{3, 3, 8}, {0, 5, 8}, {8, 0, 10}, {5, 2, 10}, {2, 4, 10}},
    {{7, 1, 10}, {4, 3, 10}, {1, 5, 10}, {9, 0, 10}, {6, 2, 10}},
};
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
    g->view.level = 1;
}

void grid_set_level(grid_t *g, uint8_t level)
{
    g->view.level = level < 1 ? 1 : (level > APP_GRID_LEVELS ? APP_GRID_LEVELS : level);
}

/* After a lost or cashed round the level drops to the number of good tiles
 * flipped (at least 1), as in the original; a clear moves up one level. */
static void settle_level(app_grid_snapshot_t *v, bool cleared_board)
{
    if (cleared_board) {
        v->level = v->level < APP_GRID_LEVELS ? (uint8_t)(v->level + 1) : APP_GRID_LEVELS;
    } else if (v->flips < v->level) {
        v->level = v->flips > 1 ? v->flips : 1;
    }
}

bool grid_mark(grid_t *g, int tile)
{
    app_grid_snapshot_t *v = &g->view;
    if (v->phase != APP_GRID_PLAYING || tile < 0 || tile >= APP_GRID_TILES || (v->revealed & (1UL << tile))) {
        return false;
    }
    v->marks[tile] = (uint8_t)((v->marks[tile] + 1) % APP_GRID_MARK_COUNT);
    return true;
}

uint32_t grid_deal(grid_t *g, uint32_t sparks)
{
    if (g->view.phase == APP_GRID_PLAYING || sparks < APP_GRID_COST) {
        return 0;
    }
    app_grid_snapshot_t *v = &g->view;
    memset(v->tiles, 1, sizeof(v->tiles));
    const uint8_t *mix = s_levels[(v->level ? v->level : 1) - 1][next_random(g) % 5U];
    scatter(g, APP_GRID_DUD, mix[2]);
    scatter(g, 2, mix[0]);
    scatter(g, 3, mix[1]);
    memset(v->marks, 0, sizeof(v->marks));
    v->flips = 0;
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
            || (v->revealed & (1UL << tile)) || v->marks[tile] == APP_GRID_MARK_DUD) {
        return 0;
    }
    v->revealed |= 1UL << tile;
    v->last_flip = (int8_t)tile;
    *events |= APP_EVENT_GRID_FLIP;
    if (v->tiles[tile] == APP_GRID_DUD) {
        v->score = 0;
        settle_level(v, false);
        finish(g, APP_GRID_RESULT_DUD, 0, events);
        return 0;
    }
    v->score = (uint16_t)(v->score * v->tiles[tile]);
    ++v->flips;
    if (cleared(v)) {
        const uint32_t won = v->score;
        settle_level(v, true);
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
    settle_level(v, false);
    finish(g, APP_GRID_RESULT_CASHED, won, events);
    return won;
}

void grid_update(grid_t *g)
{
    if (g->view.phase == APP_GRID_RESULT && g->result_ticks && --g->result_ticks == 0) {
        g->view.phase = APP_GRID_READY;
    }
}
