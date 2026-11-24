// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Where the pet's head should point (boards with a neck: the StackChan).
 * Pure logic, no hardware: the runtime feeds the model snapshot, the tick's
 * and events, and hands the pose to the neck driver,
 * whose own spring-damper filter turns these targets into motion.
 * Instance-owned; update once per fixed tick. */
#include <stdbool.h>
#include <stdint.h>
#include "../../app_game/include/app_game.h"

typedef enum {
    PET_HEAD_GESTURE_NONE = 0,
    PET_HEAD_GESTURE_NOD,      /**< Yes: petted, fed, bought. */
    PET_HEAD_GESTURE_SHAKE,    /**< No: out of sparks, refused. */
    PET_HEAD_GESTURE_PERK,     /**< Quick look up: a spark, waking. */
    PET_HEAD_GESTURE_WIGGLE,   /**< Celebration: a win, the goal, the dance. */
} pet_head_gesture_t;

typedef struct {
    float pan, tilt;           /**< Output: degrees from home, pan + = pet's left, tilt + = up. */
    uint32_t rng;
    uint32_t t;                /* Ticks since init. */
    float look_pan, look_tilt; /* Idle look-around target. */
    uint16_t look_timer;       /* Ticks until the next look-around target. */
    uint16_t still;            /* Ticks left to hold still after a touch. */
    int16_t clock_minute;      /* Minute seen last tick in clock mode, -1 = none. */
    pet_head_gesture_t gesture;
    uint16_t gesture_t, gesture_len;
} pet_head_t;

void pet_head_init(pet_head_t *head, uint32_t seed);
/** Advance one tick. `events` are this tick's APP_EVENT_* bits. */
void pet_head_update(pet_head_t *head, const app_game_snapshot_t *s, uint32_t events);
