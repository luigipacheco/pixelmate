// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <string.h>
#include "private/pet_eyes.h"

/* All geometry is in home-scale pixels and multiplied by the anchor scale. */
#define EYE_SPACING 78.0f
#define SHAPE_K 0.22f
#define SHAPE_DAMP 0.68f
#define GAZE_K 0.34f
#define GAZE_DAMP 0.55f
#define BOUNCE_K 0.18f
#define BOUNCE_DAMP 0.80f
#define GAZE_MAX_X 26.0f
#define GAZE_MAX_Y 18.0f
#define POINTER_GAIN 0.12f
#define SHAKE_TICKS 14U
#define BLINK_TICKS 6U

typedef struct {
    float w, h, r, lid, slope, cover;
} eye_target_t;

/* Shape per mood: width, height, corner radius, lid depth (fraction of height),
 * lid slope (+ droops outward = sad, - droops inward = cross), smile cover 0..1. */
static const eye_target_t s_targets[APP_MOOD_COUNT] = {
    [APP_MOOD_IDLE] = {92, 132, 30, 0, 0, 0},
    [APP_MOOD_HAPPY] = {102, 122, 36, 0, 0, 1.0f},
    [APP_MOOD_SLEEPY] = {100, 12, 6, 0, 0, 0},
    [APP_MOOD_HUNGRY] = {86, 112, 28, 0.30f, 0.35f, 0},
    [APP_MOOD_SAD] = {84, 102, 26, 0.28f, 0.45f, 0},
    [APP_MOOD_WALK] = {94, 124, 30, 0, 0, 0},
    [APP_MOOD_EATING] = {98, 96, 30, 0, 0, 0.45f},
    [APP_MOOD_THINKING] = {80, 106, 26, 0.16f, 0, 0},
    [APP_MOOD_SURPRISED] = {112, 158, 42, 0, 0, 0},
    [APP_MOOD_GRUMPY] = {94, 112, 26, 0.36f, -0.45f, 0},
    [APP_MOOD_FAINTED] = {92, 92, 0, 0, 0, 0},   /* Drawn as an X. */
};

static uint32_t rand_next(pet_eyes_t *e)
{
    uint32_t x = e->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng = x ? x : 0x9e3779b9U;
    return e->rng;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void pet_eyes_init(pet_eyes_t *e)
{
    memset(e, 0, sizeof(*e));
    e->rng = 0x51f15e5dU;
    const eye_target_t *t = &s_targets[APP_MOOD_IDLE];
    e->cur[EYE_W] = t->w;
    e->cur[EYE_H] = t->h;
    e->cur[EYE_R] = t->r;
    e->blink_timer = 50;
    e->look_timer = 40;
}

void pet_eyes_kick(pet_eyes_t *e, float impulse)
{
    e->bounce_vel += impulse;
}

void pet_eyes_shake(pet_eyes_t *e)
{
    e->shake_ticks = SHAKE_TICKS;
}

void pet_eyes_look_at(pet_eyes_t *e, float x, float y, app_eye_anchor_t a, uint16_t ticks)
{
    const float s = a.scale > 0.05f ? a.scale : 1.0f;
    e->forced_x = clampf((x - a.cx) / s * POINTER_GAIN * 2.0f, -GAZE_MAX_X, GAZE_MAX_X);
    e->forced_y = clampf((y - a.cy) / s * POINTER_GAIN * 2.0f, -GAZE_MAX_Y, GAZE_MAX_Y);
    e->forced_ticks = ticks;
}

void pet_eyes_force_gaze(pet_eyes_t *e, float gx, float gy, uint16_t ticks)
{
    e->forced_x = clampf(gx, -GAZE_MAX_X * 1.5f, GAZE_MAX_X * 1.5f);
    e->forced_y = clampf(gy, -GAZE_MAX_Y * 1.5f, GAZE_MAX_Y * 1.5f);
    e->forced_ticks = ticks;
}

static void wander(pet_eyes_t *e)
{
    if (e->look_timer) {
        --e->look_timer;
        return;
    }
    const uint32_t r = rand_next(e);
    if (r % 10U < 3U) {
        e->look_x = 0;
        e->look_y = 0;
    } else {
        e->look_x = (float)((int)(r >> 8) % 49 - 24);
        e->look_y = (float)((int)(r >> 16) % 29 - 14);
    }
    e->look_timer = (uint16_t)(45U + (r >> 4) % 110U);
}

static void gaze_target(pet_eyes_t *e, app_mood_t mood, bool down, float px, float py,
                        app_eye_anchor_t a, float *gx, float *gy)
{
    const float s = a.scale > 0.05f ? a.scale : 1.0f;
    if (down && mood != APP_MOOD_SLEEPY) {
        *gx = clampf((px - a.cx) / s * POINTER_GAIN, -GAZE_MAX_X, GAZE_MAX_X);
        *gy = clampf((py - a.cy) / s * POINTER_GAIN, -GAZE_MAX_Y, GAZE_MAX_Y);
        return;
    }
    if (e->forced_ticks) {
        --e->forced_ticks;
        *gx = e->forced_x;
        *gy = e->forced_y;
        return;
    }
    switch (mood) {
    case APP_MOOD_WALK:
        *gx = sinf(e->t * 0.07f) * 6.0f;
        *gy = -4.0f;
        return;
    case APP_MOOD_THINKING:
        *gx = 20.0f;
        *gy = -18.0f;
        return;
    case APP_MOOD_HUNGRY:
    case APP_MOOD_SAD:
        *gx = e->look_x * 0.4f;
        *gy = 14.0f;
        return;
    case APP_MOOD_SLEEPY:
    case APP_MOOD_SURPRISED:
        *gx = 0;
        *gy = 4.0f;
        return;
    default:
        wander(e);
        *gx = e->look_x;
        *gy = e->look_y;
        return;
    }
}

static void spring(float *cur, float *vel, float target, float k, float damp)
{
    *vel = *vel * damp + (target - *cur) * k;
    *cur += *vel;
}

void pet_eyes_update(pet_eyes_t *e, app_mood_t mood, bool down, float px, float py,
                     app_eye_anchor_t a)
{
    ++e->t;
    if (mood >= APP_MOOD_COUNT) {
        mood = APP_MOOD_IDLE;
    }
    eye_target_t t = s_targets[mood];
    if (mood == APP_MOOD_EATING) {
        t.h += sinf(e->t * 0.55f) * 22.0f;           /* chewing */
    } else if (mood == APP_MOOD_SLEEPY) {
        t.h += sinf(e->t * 0.08f) * 4.0f;            /* slow breathing */
    } else if (mood == APP_MOOD_HAPPY && e->t % 16U == 0U) {
        pet_eyes_kick(e, -5.0f);                     /* happy hops */
    }
    float gx = 0, gy = 0;
    gaze_target(e, mood, down, px, py, a, &gx, &gy);

    const float targets[EYE_PARAM_COUNT] = {t.w, t.h, t.r, gx, gy, t.lid, t.slope, t.cover};
    for (int i = 0; i < EYE_PARAM_COUNT; ++i) {
        const bool gaze = i == EYE_GX || i == EYE_GY;
        spring(&e->cur[i], &e->vel[i], targets[i], gaze ? GAZE_K : SHAPE_K,
               gaze ? GAZE_DAMP : SHAPE_DAMP);
    }
    spring(&e->bounce, &e->bounce_vel, 0.0f, BOUNCE_K, BOUNCE_DAMP);

    const bool can_blink = mood != APP_MOOD_SLEEPY && mood != APP_MOOD_HAPPY && mood != APP_MOOD_EATING;
    if (e->blink_phase) {
        --e->blink_phase;
    } else if (e->blink_timer) {
        --e->blink_timer;
    } else if (can_blink) {
        const uint32_t r = rand_next(e);
        e->blink_phase = BLINK_TICKS;
        e->blink_timer = r % 5U == 0U ? 10U : (uint16_t)(70U + r % 140U);  /* sometimes double-blink */
    }
    if (e->shake_ticks) {
        --e->shake_ticks;
    }
}

typedef struct {
    float cx, cy, hw, hh, r, lid, slope, cover, side;
} eye_raster_t;

static bool eye_inside(const void *shape, float x, float y)
{
    const eye_raster_t *e = shape;
    const float lx = x - e->cx, ly = y - e->cy;
    const float ax = fabsf(lx), ay = fabsf(ly);
    if (ax > e->hw || ay > e->hh) {
        return false;
    }
    const float qx = ax - (e->hw - e->r), qy = ay - (e->hh - e->r);
    if (qx > 0 && qy > 0 && qx * qx + qy * qy > e->r * e->r) {
        return false;
    }
    if (e->lid > 0.005f || fabsf(e->slope) > 0.005f) {
        const float lid_y = -e->hh + e->lid * 2.0f * e->hh + e->slope * lx * e->side;
        if (ly < lid_y) {
            return false;
        }
    }
    if (e->cover > 0.01f) {
        const float radius = e->hw * 1.5f;
        const float cy = e->hh + radius - e->cover * e->hh * 1.25f;
        if (lx * lx + (ly - cy) * (ly - cy) < radius * radius) {
            return false;
        }
    }
    return true;
}

typedef struct {
    float cx, cy, outer, inner;
} star_raster_t;

/* Five-point star: find the edge between the two vertices around the point's
 * angle and keep points on the center's side of it. */
static bool star_inside(const void *shape, float x, float y)
{
    const star_raster_t *s = shape;
    const float px = x - s->cx, py = y - s->cy;
    if (px * px + py * py > s->outer * s->outer) {
        return false;
    }
    const float step = 3.14159265f / 5.0f;
    float angle = atan2f(px, -py);
    if (angle < 0) {
        angle += 2.0f * 3.14159265f;
    }
    const int seg = (int)(angle / step);
    const float a0 = seg * step, a1 = (seg + 1) * step;
    const float r0 = (seg % 2) ? s->inner : s->outer, r1 = (seg % 2) ? s->outer : s->inner;
    const float x0 = r0 * sinf(a0), y0 = -r0 * cosf(a0);
    const float x1 = r1 * sinf(a1), y1 = -r1 * cosf(a1);
    const float edge_x = x1 - x0, edge_y = y1 - y0;
    const float side_point = edge_x * (py - y0) - edge_y * (px - x0);
    const float side_center = edge_x * (-y0) - edge_y * (-x0);
    return side_point * side_center >= 0;
}

typedef struct {
    float cx, cy, half, thick;
} cross_raster_t;

static bool cross_inside(const void *shape, float x, float y)
{
    const cross_raster_t *c = shape;
    const float ax = fabsf(x - c->cx), ay = fabsf(y - c->cy);
    return ax <= c->half && ay <= c->half && fabsf(ax - ay) <= c->thick;
}

void pet_eyes_draw(const pet_eyes_t *e, app_eye_anchor_t a, px_grid_t grid, Color color, bool stars,
                   bool cross)
{
    const float s = a.scale;
    if (s < 0.05f) {
        return;
    }
    static const float closure[BLINK_TICKS + 1] = {0, 0.3f, 0.75f, 1.0f, 1.0f, 0.8f, 0.35f};
    const float blink = closure[e->blink_phase <= BLINK_TICKS ? e->blink_phase : 0];
    const float shake = e->shake_ticks ? sinf(e->shake_ticks * 1.9f) * 9.0f * e->shake_ticks / SHAKE_TICKS : 0;
    const float min_half = grid.cell * 0.55f + 0.5f;
    for (int side = -1; side <= 1; side += 2) {
        eye_raster_t r = {
            .cx = a.cx + (side * EYE_SPACING + e->cur[EYE_GX] + shake) * s,
            .cy = a.cy + (e->cur[EYE_GY] + e->bounce) * s,
            .hw = fmaxf(e->cur[EYE_W] * (1.0f + 0.08f * blink) * 0.5f * s, min_half),
            .hh = fmaxf(e->cur[EYE_H] * (1.0f - 0.9f * blink) * 0.5f * s, min_half),
            .lid = clampf(e->cur[EYE_LID], 0.0f, 0.9f),
            .slope = e->cur[EYE_SLOPE],
            .cover = clampf(e->cur[EYE_COVER], 0.0f, 1.0f),
            .side = (float)side,
        };
        if (cross) {
            const float half = fmaxf(fminf(e->cur[EYE_W], e->cur[EYE_H]) * 0.42f * s, min_half);
            const cross_raster_t cr = {r.cx, r.cy, half, fmaxf(grid.cell * 0.75f, 9.0f * s)};
            px_fill_shape(grid, cr.cx - half - 1, cr.cy - half - 1, cr.cx + half + 1, cr.cy + half + 1,
                          cross_inside, &cr, color);
            continue;
        }
        if (stars) {
            /* Stars spin-free but still squash on blinks and bounce with the eyes. */
            const float outer = fmaxf(fminf(e->cur[EYE_W], e->cur[EYE_H]) * 0.62f * s
                                      * (1.0f - 0.85f * blink), min_half);
            const star_raster_t st = {r.cx, r.cy, outer, outer * 0.45f};
            px_fill_shape(grid, st.cx - outer - 1, st.cy - outer - 1, st.cx + outer + 1,
                          st.cy + outer + 1, star_inside, &st, color);
            continue;
        }
        r.r = clampf(e->cur[EYE_R] * s, 0.0f, fminf(r.hw, r.hh));
        px_fill_shape(grid, r.cx - r.hw - 1, r.cy - r.hh - 1, r.cx + r.hw + 1, r.cy + r.hh + 1,
                      eye_inside, &r, color);
    }
}
