// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Acts: the pet's behaviours as data. An act is a trigger plus a short script
 * of steps (expression, eye shape, color, gaze, hop, particles, sound). The
 * model decides when one plays, the view draws it. A pack of acts can be
 * replaced at run time (see docs/LEARNING.md), so the pet can learn new
 * behaviours without new firmware. Every field is range-checked: a pack can
 * make the pet look silly, never crash it. Pure C, shared with the Host
 * simulator. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_ACTS_MAX = 12,
    APP_ACT_STEPS_MAX = 8,
    APP_ACT_NAME_MAX = 12,          /**< With the terminator. A-Z, 0-9, '_' and '-'. */
    APP_ACT_TEXT_MAX = 14,          /**< With the terminator; shown as a floater. */
    APP_ACT_STEP_TICKS_MAX = 90,    /**< 3 s per step at 30 ticks a second. */
    APP_ACT_LOOK_MAX = 40,          /**< Gaze offset, home-scale pixels. */
    APP_ACT_HOP_MAX = 12,
    APP_ACT_SOUNDS = 10,            /**< Sound numbers 1..10; 0 = silent. */
    APP_ACT_COOLDOWN_MAX = 10080,   /**< Minutes (a week). */
    APP_ACTS_TEXT_MAX = 4096,       /**< Largest pack text accepted. */
    APP_ACTS_JSON_MAX = 6144,       /**< Enough for a full pack as JSON. */
};

typedef enum {
    APP_ACT_ON_NEVER = 0,   /**< Disabled. */
    APP_ACT_ON_PAT,         /**< Every `param`-th pat. */
    APP_ACT_ON_STEPS,       /**< Each time today's steps pass a multiple of `param`. */
    APP_ACT_ON_GOAL,        /**< The daily goal was reached. */
    APP_ACT_ON_FED,         /**< Today's food was walked. */
    APP_ACT_ON_WAKE,        /**< The screen woke, or the pet just started. */
    APP_ACT_ON_IDLE,        /**< `param` seconds on the home screen with no touch. */
    APP_ACT_ON_TIME,        /**< Local time from `param` to `param2`, minutes of the day. */
    APP_ACT_ON_RANDOM,      /**< About once every `param` minutes on the home screen. */
    APP_ACT_ON_COUNT,
} app_act_trigger_t;

typedef enum {
    APP_ACT_EYES_ROUND = 0, APP_ACT_EYES_STARS, APP_ACT_EYES_HEARTS, APP_ACT_EYES_SPIRALS, APP_ACT_EYES_COUNT,
} app_act_eyes_t;

typedef enum {
    APP_ACT_FX_NONE = 0, APP_ACT_FX_HEARTS, APP_ACT_FX_SPARKS, APP_ACT_FX_BOLTS, APP_ACT_FX_ZZZ,
    APP_ACT_FX_SHAKE, APP_ACT_FX_COUNT,
} app_act_fx_t;

enum {
    APP_ACT_MOOD_KEEP = 255,     /**< Leave the pet's own expression. */
    APP_ACT_COLOR_PET = 0,       /**< The pet's own eye color. */
    APP_ACT_COLOR_RAINBOW = 1,
    APP_ACT_COLOR_FIRST = 2,     /**< 2 + palette index. */
};

typedef enum {
    APP_ACT_END_DONE = 0,        /**< Played to the end. */
    APP_ACT_END_TOUCH,           /**< Cut short by a touch. */
    APP_ACT_END_OTHER,           /**< Cut short: screen changed, slept, fainted. */
} app_act_end_t;

typedef struct {
    uint8_t mood;        /**< app_mood_t IDLE..GRUMPY, or APP_ACT_MOOD_KEEP. */
    uint8_t eyes;        /**< app_act_eyes_t. */
    uint8_t color;       /**< APP_ACT_COLOR_*. */
    int8_t look_x, look_y;
    uint8_t hop;         /**< Jump at the start of the step, 0..APP_ACT_HOP_MAX. */
    uint8_t fx;          /**< app_act_fx_t at the start of the step. */
    uint8_t sound;       /**< 0, or 1..APP_ACT_SOUNDS at the start of the step. */
    uint8_t ticks;       /**< 1..APP_ACT_STEP_TICKS_MAX. */
} app_act_step_t;

typedef struct {
    uint16_t id;         /**< Chosen by the author; the diary logs it. Unique in a pack, > 0. */
    char name[APP_ACT_NAME_MAX];
    char text[APP_ACT_TEXT_MAX];   /**< Optional floater when the act starts. */
    uint8_t trigger;     /**< app_act_trigger_t. */
    uint16_t param, param2;
    uint16_t cooldown_min;   /**< Minutes before it may play again. */
    uint8_t chance;      /**< 0..100: how often a trigger actually plays it. */
    uint8_t step_count;  /**< 1..APP_ACT_STEPS_MAX. */
    app_act_step_t steps[APP_ACT_STEPS_MAX];
} app_act_t;

/** What the view needs to draw the act that is playing. */
typedef struct {
    bool active;
    uint16_t id;
    uint8_t step;
    uint8_t seq;             /**< Bumped (wraps) whenever a step starts. */
    app_act_step_t now;      /**< The step being played. */
    char text[APP_ACT_TEXT_MAX];
    uint8_t ended_seq;       /**< Bumped (wraps) whenever an act ends... */
    uint16_t ended_id;       /**< ...this one... */
    uint8_t ended_how;       /**< ...this way (app_act_end_t). */
} app_act_snapshot_t;

/** The built-in pack. Returns how many acts were written. */
int app_acts_defaults(app_act_t *out, int max);

/** True when every field of `act` is in range; else a short reason in `error`. */
bool app_act_valid(const app_act_t *act, char *error, size_t error_cap);

/** Parse a pack in the line format:
 *    act <id> <NAME> <trigger> <param> <param2> <cooldown_min> <chance>
 *    say <TEXT>                (optional)
 *    step <mood> <eyes> <color> <look_x> <look_y> <hop> <fx> <sound> <ticks>
 *  ('#' starts a comment line; numbers as in the enums above.) Returns the
 *  number of acts, or -1 with a reason in `error`. Nothing is written to `out`
 *  beyond the acts that parsed. */
int app_acts_parse(const char *text, app_act_t *out, int max, char *error, size_t error_cap);

/** Write the pack as JSON. Returns the length, or -1 if `cap` is too small. */
int app_acts_to_json(const app_act_t *acts, int count, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
