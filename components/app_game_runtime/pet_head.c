// SPDX-License-Identifier: Apache-2.0
/* Head pose for a pet with a neck. Layers, lowest first:
 *   1. a base pose: aiming at a face, a mood pose, or idle look-around;
 *   2. a short gesture on top (nod, shake, perk, wiggle) for the tick's events.
 * The screen is on the head, so while someone is touching it or using a menu
 * or a game the head holds still at home: a moving target cannot be tapped.
 * Look-around and doze numbers follow the earlier fab-stackchan character. */
#include <math.h>
#include "private/pet_head.h"

#define TICK_HZ 30
#define TWO_PI 6.2831853f

enum {
    LOOK_MIN_TICKS = 36,           /* A new look-around target every 1.2 .. 3.7 s. */
    LOOK_SPAN_TICKS = 75,
    STILL_AFTER_TOUCH_TICKS = 75,  /* Hold still 2.5 s after the last touch. */
    FACE_LOST_TICKS = 60,          /* No detection for 2 s: stop tracking. */
};

/* Degrees. Kept inside the neck driver's aim limits (40 pan, 16 tilt). */
static const float LOOK_PAN = 30.0f, LOOK_TILT = 6.0f;
/* A face at the frame edge is about this far off the camera axis (the booth's
 * square crop of the GC0308's view), and each detection closes this share of
 * the error: the camera rides on the head, so a full step would overshoot. */
static const float FACE_HALF_FOV_DEG = 24.0f, FACE_GAIN = 0.45f;
static const float FACE_PAN_MAX = 36.0f, FACE_TILT_MAX = 14.0f;

static uint32_t next_random(pet_head_t *h)
{
    h->rng = h->rng * 1664525U + 1013904223U;
    return h->rng >> 8;
}

/* Uniform in -1..1. */
static float random_unit(pet_head_t *h)
{
    return (float)(next_random(h) % 2001U) / 1000.0f - 1.0f;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void start_gesture(pet_head_t *h, pet_head_gesture_t gesture, float seconds)
{
    h->gesture = gesture;
    h->gesture_t = 0;
    h->gesture_len = (uint16_t)(seconds * TICK_HZ);
}

void pet_head_init(pet_head_t *h, uint32_t seed)
{
    *h = (pet_head_t) {.rng = seed ? seed : 0x9e3779b9U, .look_timer = LOOK_MIN_TICKS, .clock_minute = -1};
}

/* Pick the gesture for this tick's events; the strongest reaction wins. */
static void react(pet_head_t *h, const app_game_snapshot_t *s, uint32_t events)
{
    const bool won = ((events & APP_EVENT_SLOT_DONE) && s->slots.payout)
                     || ((events & APP_EVENT_GRID_DONE) && s->grid.payout)
                     || ((events & APP_EVENT_BIT_DONE) && s->bits.payout)
                     || (events & APP_EVENT_CASH_OUT);
    if (events & APP_EVENT_DANCE) {
        start_gesture(h, PET_HEAD_GESTURE_WIGGLE, 3.0f);
    } else if (won || (events & (APP_EVENT_GOAL | APP_EVENT_REVIVED))) {
        start_gesture(h, PET_HEAD_GESTURE_WIGGLE, 1.6f);
    } else if (events & (APP_EVENT_NO_SPARKS | APP_EVENT_BLOCKED)) {
        start_gesture(h, PET_HEAD_GESTURE_SHAKE, 1.0f);
    } else if (events & (APP_EVENT_ATE | APP_EVENT_BOUGHT | APP_EVENT_FED)) {
        start_gesture(h, PET_HEAD_GESTURE_NOD, 1.1f);
    } else if (events & (APP_EVENT_SPARK | APP_EVENT_WOKE)) {
        start_gesture(h, PET_HEAD_GESTURE_PERK, 0.6f);
    } else if ((events & APP_EVENT_PETTED) && h->gesture == PET_HEAD_GESTURE_NONE) {
        start_gesture(h, PET_HEAD_GESTURE_NOD, 0.8f);
    }
}

/* Offsets a running gesture adds to the base pose; fades in and out. */
static void gesture_offset(pet_head_t *h, float *pan, float *tilt)
{
    *pan = *tilt = 0.0f;
    if (h->gesture == PET_HEAD_GESTURE_NONE) {
        return;
    }
    const float t = (float)h->gesture_t / TICK_HZ;
    const float u = (float)h->gesture_t / (float)(h->gesture_len ? h->gesture_len : 1);
    const float envelope = sinf(3.1415927f * u);   /* 0 at both ends, 1 in the middle. */
    switch (h->gesture) {
    case PET_HEAD_GESTURE_NOD: *tilt = -6.0f * envelope * sinf(TWO_PI * 1.8f * t); break;
    case PET_HEAD_GESTURE_SHAKE: *pan = 11.0f * envelope * sinf(TWO_PI * 2.2f * t); break;
    case PET_HEAD_GESTURE_PERK: *tilt = 6.0f * envelope; break;
    case PET_HEAD_GESTURE_WIGGLE:
        *pan = 10.0f * envelope * sinf(TWO_PI * 1.5f * t);
        *tilt = 4.0f * envelope * sinf(TWO_PI * 3.0f * t);
        break;
    default: break;
    }
    if (++h->gesture_t >= h->gesture_len) {
        h->gesture = PET_HEAD_GESTURE_NONE;
    }
}

static void look_around(pet_head_t *h)
{
    if (h->look_timer > 0) {
        --h->look_timer;
        return;
    }
    h->look_timer = (uint16_t)(LOOK_MIN_TICKS + next_random(h) % LOOK_SPAN_TICKS);
    h->look_pan = LOOK_PAN * random_unit(h);
    h->look_tilt = LOOK_TILT * random_unit(h);
}

static void track_face(pet_head_t *h, const pet_head_face_t *face)
{
    if (face && face->seen && face->fresh) {
        if (!h->tracking) {
            h->face_pan = h->pan;      /* Start from where the head is looking. */
            h->face_tilt = h->tilt;
        }
        /* Unmirrored image: a face right of center is to the pet's right
         * (negative pan); below center is down (negative tilt). */
        h->face_pan = clampf(h->face_pan - FACE_GAIN * FACE_HALF_FOV_DEG * clampf(face->x, -1.0f, 1.0f),
                             -FACE_PAN_MAX, FACE_PAN_MAX);
        h->face_tilt = clampf(h->face_tilt - FACE_GAIN * FACE_HALF_FOV_DEG * clampf(face->y, -1.0f, 1.0f),
                              -FACE_TILT_MAX, FACE_TILT_MAX);
        h->face_lost = 0;
        h->tracking = true;
    } else if (h->tracking && ++h->face_lost >= FACE_LOST_TICKS) {
        h->tracking = false;
        h->look_pan = h->face_pan;     /* Keep looking there until the next idle target. */
        h->look_tilt = h->face_tilt;
        h->look_timer = LOOK_MIN_TICKS;
    }
}

void pet_head_update(pet_head_t *h, const app_game_snapshot_t *s, uint32_t events, const pet_head_face_t *face)
{
    if (!h || !s) {
        return;
    }
    ++h->t;
    const bool home = s->screen == APP_SCREEN_HOME && s->overlay == APP_OVERLAY_NONE;
    if (s->pointer_down) {
        h->still = STILL_AFTER_TOUCH_TICKS;
    } else if (h->still > 0) {
        --h->still;
    }
    /* Asleep, fainted, or being used: go home and hold still. */
    if (s->power == APP_POWER_DOZE || !home || s->pointer_down) {
        h->gesture = PET_HEAD_GESTURE_NONE;
        h->tracking = false;
        h->pan = 0.0f;
        h->tilt = s->fainted && home ? -12.0f : 0.0f;
        return;
    }
    react(h, s, events);
    track_face(h, face);
    /* Clock mode: a nod as the minute turns, a wiggle on the hour. */
    if (!s->clock) {
        h->clock_minute = -1;
    } else if (s->clock_minute != h->clock_minute) {
        if (h->clock_minute >= 0) {
            start_gesture(h, s->clock_minute % 60 == 0 ? PET_HEAD_GESTURE_WIGGLE : PET_HEAD_GESTURE_NOD,
                          s->clock_minute % 60 == 0 ? 2.4f : 0.8f);
        }
        h->clock_minute = s->clock_minute;
    }
    float pan = 0.0f, tilt = 0.0f;
    const float t = (float)h->t / TICK_HZ;
    if (s->fainted) {
        tilt = -12.0f;                               /* Head down, no motion. */
    } else if (h->tracking) {
        pan = h->face_pan;
        tilt = h->face_tilt;
    } else if (s->clock) {
        tilt = 3.0f;                                 /* Clock mode: face forward so the time can be read. */
    } else if (s->mood == APP_MOOD_SLEEPY || s->power == APP_POWER_DIM) {
        pan = 5.0f * sinf(0.25f * t);                /* Dozing: sagging, slow sway. */
        tilt = -6.0f - 1.5f * sinf(0.4f * t);
    } else if (h->still > 0) {
        /* Just touched: face the person, level. */
    } else if (s->mood == APP_MOOD_HUNGRY || s->mood == APP_MOOD_SAD) {
        look_around(h);
        pan = 0.5f * h->look_pan;                    /* Listless: smaller looks, head low. */
        tilt = -5.0f;
    } else {
        look_around(h);
        pan = h->look_pan;
        tilt = h->look_tilt;
    }
    float gesture_pan, gesture_tilt;
    gesture_offset(h, &gesture_pan, &gesture_tilt);
    h->pan = pan + gesture_pan;
    h->tilt = tilt + gesture_tilt;
}
