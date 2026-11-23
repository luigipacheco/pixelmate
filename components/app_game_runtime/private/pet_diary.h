// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The pet's diary: a ring of small timestamped events (walks, pats, meals,
 * games, acts played and how they ended) that a "brain" program on a computer
 * reads over the network to learn from (docs/LEARNING.md). The recorder only
 * watches model snapshots and event bits; it never changes the model. Not
 * thread-safe: the runtime guards it. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../app_game/include/app_game.h"

enum {
    PET_DIARY_ENTRIES = 512,
    PET_DIARY_MAGIC = 0x44495231,      /* "DIR1" */
    PET_DIARY_WALK_TICKS = 5 * 60 * 30,    /* Steps are logged in 5 minute pieces. */
    PET_DIARY_PAT_TICKS = 60 * 30,         /* Plain pats are counted per minute. */
    PET_DIARY_AFTER_ACT_TICKS = 10 * 30,   /* A pat this soon after an act is feedback on it. */
};

typedef enum {
    PET_DIARY_NONE = 0,
    PET_DIARY_BOOT,      /* arg: 1 = woke from pocket deep sleep. */
    PET_DIARY_PAT,       /* arg 0: value = pats in the last minute. arg 1: a pat during or right after act `value`. */
    PET_DIARY_WALK,      /* value = steps in the last 5 minutes; arg 1 = counted while asleep. */
    PET_DIARY_FEED,      /* arg = snack index. */
    PET_DIARY_GAME,      /* arg = app_arcade_t, value = sparks paid. */
    PET_DIARY_ACT,       /* value = act id, arg = app_act_end_t. */
    PET_DIARY_SCREEN,    /* arg = app_screen_t. */
    PET_DIARY_POWER,     /* arg = app_power_t (awake or asleep). */
    PET_DIARY_GOAL,      /* value = today's steps / 10. */
    PET_DIARY_FED,
    PET_DIARY_DAY,       /* A day ended: value = its steps / 10, arg = health now. */
    PET_DIARY_BUY,
    PET_DIARY_REVIVE,
    PET_DIARY_TYPE_COUNT,
} pet_diary_type_t;

typedef struct {
    uint32_t t;          /* Seconds since 1970, or 0 while the clock is unknown. */
    uint8_t type, arg;
    uint16_t value;
} pet_diary_entry_t;

/* The part that is kept across restarts, as is. */
typedef struct {
    uint32_t magic;
    uint32_t total;      /* Entries ever written; entry n lives at n % PET_DIARY_ENTRIES. */
    pet_diary_entry_t e[PET_DIARY_ENTRIES];
} pet_diary_store_t;

typedef struct {
    pet_diary_store_t *store;
    uint32_t added;              /* Entries written since the last pet_diary_saved(). */
    bool primed;
    uint32_t walk_base, walk_ticks;
    uint16_t pats, pat_ticks;
    uint8_t act_ended_seq, cue_seq;
    uint32_t act_end_tick;
    uint16_t act_last_id;
    app_power_t power;
} pet_diary_t;

/** Use `store` (kept if its magic is right, else cleared). */
void pet_diary_init(pet_diary_t *d, pet_diary_store_t *store);
void pet_diary_add(pet_diary_t *d, uint32_t now_s, pet_diary_type_t type, uint8_t arg, uint32_t value);
/** Once per tick: turn events and snapshot changes into entries. */
void pet_diary_observe(pet_diary_t *d, uint32_t now_s, uint32_t events, const app_game_snapshot_t *s);
/** Write the walk and pats counted so far (before sleeping). */
void pet_diary_flush(pet_diary_t *d, uint32_t now_s, const app_game_snapshot_t *s);
/** Entries from number `since` on, as comma-separated JSON objects. Returns the
 *  bytes written (0 when there are no more) and the number to ask for next. */
int pet_diary_json(const pet_diary_store_t *store, uint32_t since, char *out, size_t cap, uint32_t *next);
/** When (seconds since 1970) act `id` last ended, or 0. */
uint32_t pet_diary_last_act(const pet_diary_store_t *store, uint16_t id);
