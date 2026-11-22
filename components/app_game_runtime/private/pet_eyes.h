// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The pet's face: two eyes whose shape parameters chase per-mood targets
 * through damped springs (jelly overshoot), plus blinks, gaze and bounce.
 * Instance-owned; update at the fixed tick, draw in render. */
#include <stdint.h>
#include "pixel_draw.h"
#include "../../app_game/include/app_game.h"
#include "../../app_game/include/app_game_layout.h"

enum {
    EYE_W, EYE_H, EYE_R, EYE_GX, EYE_GY, EYE_LID, EYE_SLOPE, EYE_COVER, EYE_PARAM_COUNT
};

typedef struct {
    float cur[EYE_PARAM_COUNT];
    float vel[EYE_PARAM_COUNT];
    float bounce, bounce_vel;     /* Vertical offset spring, pixels. */
    float look_x, look_y;         /* Current wander target, home-scale pixels. */
    float forced_x, forced_y;     /* Look-at override target. */
    uint16_t forced_ticks;
    uint16_t look_timer;
    uint16_t blink_timer;         /* Ticks until the next blink starts. */
    uint8_t blink_phase;          /* 0 = open, counts down while blinking. */
    uint16_t shake_ticks;
    uint32_t rng;
    uint32_t t;
} pet_eyes_t;

void pet_eyes_init(pet_eyes_t *eyes);
/** Advance one tick toward the mood's shape. pointer_* follow the finger when down. */
void pet_eyes_update(pet_eyes_t *eyes, app_mood_t mood, bool pointer_down,
                     float pointer_x, float pointer_y, app_eye_anchor_t anchor);
/** Add a vertical impulse (negative = jump up). */
void pet_eyes_kick(pet_eyes_t *eyes, float impulse);
void pet_eyes_shake(pet_eyes_t *eyes);
/** Look toward a screen point for `ticks` ticks. */
void pet_eyes_look_at(pet_eyes_t *eyes, float x, float y, app_eye_anchor_t anchor, uint16_t ticks);
/** Override gaze directly in home-scale pixels (dances, scripted looks). */
void pet_eyes_force_gaze(pet_eyes_t *eyes, float gx, float gy, uint16_t ticks);
typedef enum {
    PET_EYES_ROUND = 0,
    PET_EYES_STARS,        /**< STAR EYES when happy. */
    PET_EYES_CROSS,        /**< Fainted. */
    PET_EYES_HEARTS,       /**< LOVE EYES (1000 pats) when happy. */
    PET_EYES_SPIRALS,      /**< Dizzy after a hard shake. */
} pet_eye_shape_t;

/** Draw both eyes in `shape`; `headband` adds the MARATHON band above them. */
void pet_eyes_draw(const pet_eyes_t *eyes, app_eye_anchor_t anchor, px_grid_t grid, Color color,
                   pet_eye_shape_t shape, bool headband);
