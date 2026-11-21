// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "private/pet_screens.h"
#include "private/pet_view.h"
#include "../app_game/include/app_game_layout.h"

enum {
    P_SQUARE = 0, P_HEART, P_BOLT, P_Z,
};
enum {
    TRANSITION_TICKS = 12,
    FOOD_TICKS = 16,
    FLIP_TICKS = 10,
    DEAL_TICKS = 12,
    SLIDE_TICKS = 10,
    FLOATER_TICKS = 40,
    SPARK_PULSE_TICKS = 10,
    SPARK_FLASH_TICKS = 24,
    Z_PERIOD_TICKS = 34,
    REVEAL_LOOK_TICKS = 30,
};
static const Color WARN = {255, 48, 48, 255};
static const Color CONFETTI_WHITE = {240, 240, 232, 255};

static uint32_t rand_next(pet_view_t *v)
{
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x ? x : 0x2f6b1d3dU;
    return v->rng;
}

static float rand_range(pet_view_t *v, float lo, float hi)
{
    return lo + (hi - lo) * (float)(rand_next(v) % 10000U) / 10000.0f;
}

static void spawn(pet_view_t *v, uint8_t kind, float x, float y, float vx, float vy,
                  float gravity, uint16_t life, Color color)
{
    view_particle_t *p = &v->particles[v->particle_cursor];
    v->particle_cursor = (uint8_t)((v->particle_cursor + 1U) % VIEW_PARTICLES);
    *p = (view_particle_t) {
        x, y, vx, vy, gravity, life, life, kind, color
    };
}

static void burst(pet_view_t *v, uint8_t kind, float x, float y, int count, float speed,
                  float gravity, Color a, Color b)
{
    for (int i = 0; i < count; ++i) {
        const float angle = rand_range(v, 0, 6.2831853f);
        const float s = rand_range(v, speed * 0.4f, speed);
        spawn(v, kind, x, y, cosf(angle) * s, sinf(angle) * s - speed * 0.5f, gravity,
              (uint16_t)rand_range(v, 22, 40), i % 2 ? a : b);
    }
}

static void floater(pet_view_t *v, const char *text, float x, float y, Color color)
{
    view_floater_t *f = &v->floaters[v->floater_cursor];
    v->floater_cursor = (uint8_t)((v->floater_cursor + 1U) % VIEW_FLOATERS);
    snprintf(f->text, sizeof(f->text), "%s", text);
    f->x = x;
    f->y = y;
    f->life = FLOATER_TICKS;
    f->color = color;
}

static app_eye_anchor_t current_anchor(const pet_view_t *v)
{
    const app_eye_anchor_t to = app_eye_anchor(v->screen);
    if (v->trans >= 1.0f) {
        return to;
    }
    const app_eye_anchor_t from = app_eye_anchor(v->prev_screen);
    const float e = screens_ease_out(v->trans);
    return (app_eye_anchor_t) {
        (int16_t)(from.cx + (to.cx - from.cx) * e),
        (int16_t)(from.cy + (to.cy - from.cy) * e),
        from.scale + (to.scale - from.scale) * e,
    };
}

static void advance(float *t, int ticks)
{
    if (*t < 1.0f) {
        *t += 1.0f / (float)ticks;
    }
}

void pet_view_init(pet_view_t *v)
{
    memset(v, 0, sizeof(*v));
    pet_eyes_init(&v->eyes);
    v->rng = 0x7a3c19e1U;
    v->trans = v->food_t = v->flip_t = v->deal_t = v->slide_t = 1.0f;
    v->food_index = -1;
}

static void on_events(pet_view_t *v, const app_game_snapshot_t *s, uint32_t ev)
{
    const Color c = px_color(s->color_index), acc = px_accent(s->color_index);
    const app_eye_anchor_t a = current_anchor(v);
    if (ev & APP_EVENT_SCREEN && s->screen != v->screen) {
        v->prev_screen = v->screen;
        v->screen = s->screen;
        v->trans_dir = v->screen > v->prev_screen ? 1 : -1;
        v->trans = 0;
    }
    const int32_t gained = (int32_t)(s->sparks - v->prev_sparks);
    if (gained > 0 && (ev & (APP_EVENT_SPARK | APP_EVENT_CASH_OUT))) {
        char text[VIEW_FLOATER_TEXT];
        snprintf(text, sizeof(text), "+%ld", (long)gained);
        floater(v, text, 400, 64, acc);
        v->spark_pulse = SPARK_PULSE_TICKS;
        burst(v, P_BOLT, 440, 28, 4, 4.0f, 0.25f, c, acc);
    }
    if (ev & APP_EVENT_STEP) {
        pet_eyes_kick(&v->eyes, -3.5f);
    }
    if (ev & APP_EVENT_GOAL) {
        floater(v, "GOAL!", 240, 300, acc);
        burst(v, P_SQUARE, 240, 120, 30, 9.0f, 0.35f, c, CONFETTI_WHITE);
        burst(v, P_SQUARE, 240, 120, 16, 7.0f, 0.35f, acc, c);
    }
    if (ev & APP_EVENT_NO_SPARKS) {
        v->spark_flash = SPARK_FLASH_TICKS;
        pet_eyes_shake(&v->eyes);
    }
    if (ev & APP_EVENT_ATE && s->last_food >= 0) {
        v->food_index = s->last_food;
        v->food_t = 0;
    }
    if (ev & APP_EVENT_PETTED) {
        for (int i = 0; i < 3; ++i) {
            spawn(v, P_HEART, a.cx + rand_range(v, -90, 90) * a.scale, a.cy - 70 * a.scale,
                  rand_range(v, -1.2f, 1.2f), rand_range(v, -3.5f, -2.0f), 0.02f, 34, acc);
        }
    }
    if (ev & APP_EVENT_CARD_DEAL) {
        v->deal_t = 0;
    }
    if (ev & APP_EVENT_CARD_REVEAL) {
        v->flip_t = 0;
        const app_rect_t slot = app_cards_slot_rect(1);
        pet_eyes_look_at(&v->eyes, slot.x + slot.w / 2, slot.y + slot.h / 2, a, REVEAL_LOOK_TICKS);
        switch (s->cards.result) {
        case APP_CARD_RESULT_WIN:
            floater(v, "x2", 312, 196, acc);
            burst(v, P_BOLT, 312, 256, 8, 6.0f, 0.3f, c, acc);
            break;
        case APP_CARD_RESULT_LOSE:
            floater(v, "LOSE", 312, 196, WARN);
            pet_eyes_kick(&v->eyes, 7.0f);
            break;
        default:
            floater(v, "TIE", 312, 196, c);
            break;
        }
    }
    if (ev & APP_EVENT_CARD_NEXT) {
        v->slide_t = 0;
    }
    if (ev & APP_EVENT_CASH_OUT) {
        burst(v, P_BOLT, 240, 250, 10, 7.0f, 0.3f, c, acc);
    }
    if (ev & APP_EVENT_WOKE) {
        pet_eyes_kick(&v->eyes, -10.0f);
    }
    if (ev & APP_EVENT_SETTINGS) {
        pet_eyes_kick(&v->eyes, -6.0f);
    }
}

static void update_food(pet_view_t *v, const app_game_snapshot_t *s)
{
    if (v->food_t >= 1.0f) {
        return;
    }
    v->food_t += 1.0f / FOOD_TICKS;
    if (v->food_t < 1.0f) {
        return;
    }
    const app_eye_anchor_t a = current_anchor(v);
    const Color acc = px_accent(s->color_index);
    burst(v, P_SQUARE, a.cx, a.cy + 50 * a.scale, 12, 5.0f, 0.4f, acc, px_color(s->color_index));
    for (int i = 0; i < 2; ++i) {
        spawn(v, P_HEART, a.cx + (i ? 60 : -60) * a.scale, a.cy - 60 * a.scale,
              0, -2.2f, 0.0f, 32, acc);
    }
    pet_eyes_kick(&v->eyes, -7.0f);
}

void pet_view_update(pet_view_t *v, const app_game_snapshot_t *s, uint32_t events)
{
    ++v->t;
    if (!v->primed) {
        v->primed = true;
        v->screen = v->prev_screen = s->screen;
        v->steps_shown = (float)s->steps_today;
        v->sparks_shown = (float)s->sparks;
        v->prev_sparks = s->sparks;
    }
    on_events(v, s, events);
    v->prev_sparks = s->sparks;

    advance(&v->trans, TRANSITION_TICKS);
    advance(&v->flip_t, FLIP_TICKS);
    advance(&v->deal_t, DEAL_TICKS);
    advance(&v->slide_t, SLIDE_TICKS);
    update_food(v, s);
    if (v->spark_pulse) {
        --v->spark_pulse;
    }
    if (v->spark_flash) {
        --v->spark_flash;
    }

    /* Count-ups ease toward the model value; the ring fills with them. */
    v->steps_shown += ((float)s->steps_today - v->steps_shown) * 0.18f;
    if (fabsf((float)s->steps_today - v->steps_shown) < 0.5f) {
        v->steps_shown = (float)s->steps_today;
    }
    v->sparks_shown += ((float)s->sparks - v->sparks_shown) * 0.25f;
    if (fabsf((float)s->sparks - v->sparks_shown) < 0.5f) {
        v->sparks_shown = (float)s->sparks;
    }
    const float goal = s->step_goal ? (float)s->steps_today / (float)s->step_goal : 0;
    v->ring_shown += ((goal > 1.0f ? 1.0f : goal) - v->ring_shown) * 0.12f;

    const app_eye_anchor_t a = current_anchor(v);
    if (s->mood == APP_MOOD_SLEEPY && v->t % Z_PERIOD_TICKS == 0U && a.scale > 0.05f) {
        spawn(v, P_Z, a.cx + 110 * a.scale, a.cy - 50 * a.scale, 0.8f, -1.4f, 0.0f, 50,
              px_color(s->color_index));
    }
    for (int i = 0; i < VIEW_PARTICLES; ++i) {
        view_particle_t *p = &v->particles[i];
        if (!p->life) {
            continue;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->vy += p->gravity;
        --p->life;
    }
    for (int i = 0; i < VIEW_FLOATERS; ++i) {
        view_floater_t *f = &v->floaters[i];
        if (f->life) {
            f->y -= 1.3f;
            --f->life;
        }
    }
    pet_eyes_update(&v->eyes, s->mood, s->pointer_down, (float)s->pointer_x, (float)s->pointer_y, a);
}

/* ---- Drawing --------------------------------------------------------- */

static void draw_particles(const pet_view_t *v, const draw_ctx_t *d)
{
    const int cell = d->grid.cell < 6 ? 6 : d->grid.cell;
    for (int i = 0; i < VIEW_PARTICLES; ++i) {
        const view_particle_t *p = &v->particles[i];
        if (!p->life || (p->life < 8 && (p->life & 1U))) {
            continue;
        }
        const int x = (int)p->x, y = (int)p->y;
        switch (p->kind) {
        case P_HEART: px_icon(PX_ICON_HEART, x - 10, y - 9, 3, p->color, p->color, d->gap); break;
        case P_BOLT: px_icon(PX_ICON_BOLT, x - 6, y - 7, 2, p->color, p->color, false); break;
        case P_Z: px_text("Z", x, y, p->life > 25 ? 4 : 3, p->color, d->gap); break;
        default: DrawRectangle(x, y, cell - d->grid.gap, cell - d->grid.gap, p->color); break;
        }
    }
    for (int i = 0; i < VIEW_FLOATERS; ++i) {
        const view_floater_t *f = &v->floaters[i];
        if (f->life && !(f->life < 8 && (f->life & 1U))) {
            px_text_center(f->text, (int)f->x, (int)f->y, 4, f->color, d->gap);
        }
    }
}

static void draw_food_toss(const pet_view_t *v, const draw_ctx_t *d)
{
    if (v->food_t >= 1.0f || v->food_index < 0 || v->food_index >= APP_FOOD_COUNT) {
        return;
    }
    const app_rect_t r = app_feed_item_rect(v->food_index);
    const app_eye_anchor_t a = current_anchor(v);
    const float t = v->food_t;
    const float sx = r.x + r.w / 2.0f, sy = r.y + 64.0f;
    const float ex = a.cx, ey = a.cy + 40 * a.scale;
    const float x = sx + (ex - sx) * t;
    const float y = sy + (ey - sy) * t - sinf(t * 3.14159f) * 90.0f;
    const int scale = t < 0.5f ? 6 : (t < 0.8f ? 5 : 4);
    const char *const *icon = PX_ICON_FOOD[v->food_index];
    px_icon(icon, (int)x - px_icon_width(icon) * scale / 2, (int)y - px_icon_height(icon) * scale / 2,
            scale, d->color, d->accent, d->gap);
}

static void draw_top_bar(const pet_view_t *v, const draw_ctx_t *d)
{
    char text[16];
    px_icon(PX_ICON_SHOE, 18, 18, 3, d->color, d->accent, d->gap);
    snprintf(text, sizeof(text), "%lu", (unsigned long)(v->steps_shown + 0.5f));
    px_text(text, 50, 18, 3, d->color, d->gap);

    const int scale = v->spark_pulse ? 4 : 3;
    const bool flash = v->spark_flash && ((v->t / 3U) & 1U);
    const Color c = flash ? WARN : d->color;
    snprintf(text, sizeof(text), "%lu", (unsigned long)(v->sparks_shown + 0.5f));
    const int bolt_x = APP_VIEW_W - 18 - 6 * scale;
    px_icon(PX_ICON_BOLT, bolt_x, 18 - (scale - 3) * 3, scale, flash ? WARN : d->accent, c, d->gap);
    px_text(text, bolt_x - 8 - px_text_width(text, scale), 18 - (scale - 3) * 3, scale, c, d->gap);
}

static void draw_dots(const pet_view_t *v, const draw_ctx_t *d)
{
    const int spacing = 24, size = 10;
    const int x0 = APP_VIEW_W / 2 - (APP_SCREEN_COUNT - 1) * spacing / 2 - size / 2;
    for (int i = 0; i < APP_SCREEN_COUNT; ++i) {
        DrawRectangle(x0 + i * spacing, APP_DOTS_Y, size, size, d->dim);
    }
    const float e = screens_ease_out(v->trans);
    const float pos = v->prev_screen + ((float)v->screen - (float)v->prev_screen) * e;
    DrawRectangle(x0 + (int)(pos * spacing) - 2, APP_DOTS_Y - 2, size + 4, size + 4, d->color);
}

void pet_view_draw(const pet_view_t *v, const app_game_snapshot_t *s)
{
    const px_grid_t grid = px_grid(s->pixel_style);
    const Color c = px_color(s->color_index);
    const draw_ctx_t d = {
        .view = v, .snap = s, .grid = grid, .gap = grid.gap > 0,
        .color = c, .accent = px_accent(s->color_index), .dim = px_scale(c, 0.24f),
    };
    ClearBackground(BLACK);
    if (v->trans < 1.0f) {
        const float e = screens_ease_out(v->trans);
        screens_draw(&d, v->prev_screen, (int)(-v->trans_dir * APP_VIEW_W * e));
        screens_draw(&d, v->screen, (int)(v->trans_dir * APP_VIEW_W * (1.0f - e)));
    } else {
        screens_draw(&d, v->screen, 0);
    }
    pet_eyes_draw(&v->eyes, current_anchor(v), grid, c);
    draw_food_toss(v, &d);
    draw_particles(v, &d);
    draw_top_bar(v, &d);
    draw_dots(v, &d);
}
