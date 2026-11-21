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
    OVERLAY_TICKS = 8,
    CRT_TICKS = 12,
    CRT_TOP = 60, CRT_BOTTOM = 446,
    DANCE_TICKS = 72,
};
#define ANCHOR_EASE 0.25f
#define DIM_LEVEL 0.3f
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
    return (app_eye_anchor_t) {
        (int16_t)v->anchor_x, (int16_t)v->anchor_y, v->anchor_s
    };
}

/* Eyes glide toward where the current screen/panel wants them. */
static void ease_anchor(pet_view_t *v, const app_game_snapshot_t *s, bool snap)
{
    const app_eye_anchor_t to = app_eye_anchor(s->screen, s->overlay, s->arcade);
    const float k = snap ? 1.0f : ANCHOR_EASE;
    v->anchor_x += (to.cx - v->anchor_x) * k;
    v->anchor_y += (to.cy - v->anchor_y) * k;
    v->anchor_s += (to.scale - v->anchor_s) * k;
}

/* Retract the drawn panel before dropping a different one. */
static void update_overlay(pet_view_t *v, app_overlay_t target)
{
    if (v->overlay_drawn != target) {
        if (v->overlay_drawn != APP_OVERLAY_NONE && v->overlay_pos > 0) {
            v->overlay_pos -= 1.0f / OVERLAY_TICKS;
            if (v->overlay_pos > 0) {
                return;
            }
        }
        v->overlay_drawn = target;
        v->overlay_pos = 0;
    }
    if (v->overlay_drawn != APP_OVERLAY_NONE && v->overlay_pos < 1.0f) {
        v->overlay_pos += 1.0f / OVERLAY_TICKS;
        if (v->overlay_pos > 1.0f) {
            v->overlay_pos = 1.0f;
        }
    }
}

static Color rainbow(uint32_t t)
{
    /* Hue wheel in six linear segments. */
    const float h = (float)(t % 90U) / 15.0f;
    const int seg = (int)h;
    const float f = h - seg;
    const unsigned char up = (unsigned char)(255 * f), down = (unsigned char)(255 * (1 - f));
    switch (seg) {
    case 0: return (Color) {255, up, 0, 255};
    case 1: return (Color) {down, 255, 0, 255};
    case 2: return (Color) {0, 255, up, 255};
    case 3: return (Color) {0, down, 255, 255};
    case 4: return (Color) {up, 0, 255, 255};
    default: return (Color) {255, 0, down, 255};
    }
}

static bool anim_on(const app_game_snapshot_t *s, app_anim_t anim)
{
    return (s->anims_on & (1U << anim)) != 0;
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
    v->trans = v->food_t = v->flip_t = v->deal_t = v->slide_t = v->crt_t = v->grid_flip_t = 1.0f;
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
        if (anim_on(s, APP_ANIM_TRAIL) && a.scale > 0.05f) {
            for (int side = -1; side <= 1; side += 2) {
                spawn(v, P_BOLT, a.cx + side * 78 * a.scale + rand_range(v, -20, 20) * a.scale,
                      a.cy + 60 * a.scale, rand_range(v, -0.8f, 0.8f), rand_range(v, 0.5f, 2.0f),
                      0.25f, 26, side < 0 ? c : acc);
            }
        }
    }
    if (ev & APP_EVENT_DANCE) {
        char text[VIEW_FLOATER_TEXT];
        snprintf(text, sizeof(text), "%lu", (unsigned long)(s->steps_total / 100U * 100U));
        floater(v, text, 240, 90, acc);
        v->dance_ticks = DANCE_TICKS;
    }
    if (ev & APP_EVENT_SLOT_SPIN) {
        pet_eyes_kick(&v->eyes, -5.0f);
        pet_eyes_force_gaze(&v->eyes, 0, 18.0f, 20);   /* Look down at the reels. */
    }
    const uint8_t newly = (uint8_t)(s->slots.stopped & ~v->prev_slot_stopped);
    for (int reel = 0; reel < APP_SLOT_REELS; ++reel) {
        if (newly & (1U << reel)) {
            const app_rect_t w = app_slots_window_rect(reel);
            burst(v, P_SQUARE, w.x + w.w / 2, w.y + w.h / 2, 6, 4.0f, 0.3f, acc, c);
            pet_eyes_force_gaze(&v->eyes, (reel - 1) * 24.0f, 16.0f, 12);
        }
    }
    v->prev_slot_stopped = s->slots.stopped;
    if (ev & APP_EVENT_SLOT_DONE && s->slots.payout) {
        char text[VIEW_FLOATER_TEXT];
        const bool jackpot = s->slots.payout >= app_game_slot_triple_pay(APP_SLOT_SEVEN);
        snprintf(text, sizeof(text), jackpot ? "JACKPOT!" : "+%u", s->slots.payout);
        floater(v, text, 240, 300, acc);
        const int amount = s->slots.payout >= 30 ? 30 : 8;
        burst(v, P_BOLT, 240, 240, amount / 2, 8.0f, 0.3f, c, acc);
        burst(v, P_SQUARE, 240, 240, amount, 9.0f, 0.35f, acc, CONFETTI_WHITE);
        v->spark_pulse = SPARK_PULSE_TICKS;
        if (jackpot) {
            v->dance_ticks = DANCE_TICKS;
        }
    }
    if (ev & APP_EVENT_BOUGHT) {
        floater(v, "NEW!", 240, 300, acc);
        burst(v, P_SQUARE, 240, 200, 26, 9.0f, 0.35f, c, CONFETTI_WHITE);
        burst(v, P_BOLT, 240, 200, 8, 7.0f, 0.3f, acc, c);
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
    if (ev & APP_EVENT_ARCADE && s->arcade != APP_ARCADE_LOBBY) {
        v->crt_t = 0;
    }
    if (ev & APP_EVENT_CARD_DEAL && s->arcade == APP_ARCADE_GRID) {
        v->grid_flipped = 0;   /* New grid round. */
    }
    if (ev & APP_EVENT_GRID_FLIP && s->grid.last_flip >= 0) {
        v->grid_flip_t = 0;
        v->grid_flipped |= 1UL << s->grid.last_flip;
    }
    if (ev & APP_EVENT_GRID_DONE) {
        const app_rect_t t = app_grid_tile_rect(s->grid.last_flip >= 0 ? s->grid.last_flip : 12);
        if (s->grid.result == APP_GRID_RESULT_DUD) {
            pet_eyes_shake(&v->eyes);
            burst(v, P_SQUARE, t.x + t.w / 2, t.y + t.h / 2, 14, 7.0f, 0.35f, WARN, CONFETTI_WHITE);
        } else {
            char text[VIEW_FLOATER_TEXT];
            snprintf(text, sizeof(text), "+%u", s->grid.payout);
            floater(v, text, 186, 250, acc);
            burst(v, P_BOLT, 186, 250, 8, 7.0f, 0.3f, c, acc);
            if (s->grid.result == APP_GRID_RESULT_CLEARED) {
                burst(v, P_SQUARE, 186, 250, 26, 9.0f, 0.35f, acc, CONFETTI_WHITE);
            }
            v->spark_pulse = SPARK_PULSE_TICKS;
        }
    }
    if (ev & APP_EVENT_BLOCKED) {
        pet_eyes_shake(&v->eyes);
    }
    if (ev & APP_EVENT_REVIVED) {
        floater(v, "REVIVED!", 240, 300, acc);
        burst(v, P_HEART, 240, 230, 8, 6.0f, 0.15f, acc, c);
        burst(v, P_SQUARE, 240, 230, 24, 9.0f, 0.35f, c, CONFETTI_WHITE);
    } else if (ev & APP_EVENT_FED) {
        floater(v, "FED!", 240, 300, acc);
        burst(v, P_HEART, 240, 230, 5, 5.0f, 0.15f, acc, c);
    }
    if (ev & APP_EVENT_DAY && s->hp != v->prev_hp) {
        floater(v, s->hp > v->prev_hp ? "+1 HP" : "-1 HP", 240, 120, s->hp > v->prev_hp ? acc : WARN);
    }
    v->prev_hp = s->hp;
    if (ev & APP_EVENT_POWER && s->power == APP_POWER_AWAKE) {
        /* Snap the step/spark count-ups so the wake frame is current. */
        v->steps_shown = (float)s->steps_today;
        v->sparks_shown = (float)s->sparks;
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
        v->prev_hp = s->hp;
        ease_anchor(v, s, true);
    }
    ease_anchor(v, s, false);
    update_overlay(v, s->overlay);
    on_events(v, s, events);
    v->prev_sparks = s->sparks;
    if (v->dance_ticks) {
        /* Side-to-side shimmy with a hop on every beat. */
        --v->dance_ticks;
        const float beat = (v->dance_ticks / 9U) % 2U ? 1.0f : -1.0f;
        pet_eyes_force_gaze(&v->eyes, beat * 30.0f, -6.0f, 2);
        if (v->dance_ticks % 9U == 0U) {
            pet_eyes_kick(&v->eyes, -7.0f);
        }
    }

    advance(&v->trans, TRANSITION_TICKS);
    advance(&v->flip_t, FLIP_TICKS);
    advance(&v->deal_t, DEAL_TICKS);
    advance(&v->slide_t, SLIDE_TICKS);
    advance(&v->crt_t, CRT_TICKS);
    advance(&v->grid_flip_t, FLIP_TICKS);
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
              px_scale(px_color(s->color_index), s->power == APP_POWER_DIM ? DIM_LEVEL : 1.0f));
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

/* Old-TV power-on: a bright line sweeps out, then the picture opens from it. */
static void draw_crt(const pet_view_t *v, const draw_ctx_t *d)
{
    if (v->crt_t >= 1.0f || d->snap->screen != APP_SCREEN_ARCADE) {
        return;
    }
    const int mid = (CRT_TOP + CRT_BOTTOM) / 2, half = (CRT_BOTTOM - CRT_TOP) / 2;
    const float line_end = 0.35f;
    if (v->crt_t < line_end) {
        const int w = (int)(APP_VIEW_W * v->crt_t / line_end);
        DrawRectangle(0, CRT_TOP, APP_VIEW_W, CRT_BOTTOM - CRT_TOP, BLACK);
        DrawRectangle(APP_VIEW_W / 2 - w / 2, mid - 2, w, 4, CONFETTI_WHITE);
        return;
    }
    const float open = screens_ease_out((v->crt_t - line_end) / (1.0f - line_end));
    const int band = (int)(half * (1.0f - open));
    DrawRectangle(0, CRT_TOP, APP_VIEW_W, band, BLACK);
    DrawRectangle(0, CRT_BOTTOM - band, APP_VIEW_W, band, BLACK);
    if (band > 2) {
        /* Glowing edges where the picture is opening. */
        DrawRectangle(0, CRT_TOP + band, APP_VIEW_W, 2, d->color);
        DrawRectangle(0, CRT_BOTTOM - band - 2, APP_VIEW_W, 2, d->color);
    }
}

static void draw_steps_counter(const pet_view_t *v, const draw_ctx_t *d)
{
    char text[16];
    px_icon(PX_ICON_SHOE, 18, 18, 3, d->color, d->accent, d->gap);
    snprintf(text, sizeof(text), "%lu", (unsigned long)(v->steps_shown + 0.5f));
    px_text(text, 50, 18, 3, d->color, d->gap);
    /* Grabber: pull down from here for the menu. */
    DrawRectangle(216, 6, 48, 4, d->dim);
    DrawRectangle(228, 14, 24, 4, d->dim);
    /* Battery only when it matters: low (blinking) or charging. */
    const app_game_snapshot_t *s = d->snap;
    if (s->battery_pct >= 0) {
        const bool low = s->battery_pct <= APP_LOW_BATTERY_PCT && !s->battery_charging;
        if (s->battery_charging || (low && (v->t / 15U) % 2U)) {
            px_battery(296, 22, s->battery_pct, s->battery_charging, d->color, d->accent);
        }
    }
}

/* Drawn above panels too, so the store always shows the balance. */
static void draw_sparks_counter(const pet_view_t *v, const draw_ctx_t *d)
{
    char text[16];
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
    const int pages = APP_SCREEN_COUNT;
    const int x0 = APP_VIEW_W / 2 - (pages - 1) * spacing / 2 - size / 2;
    for (int i = 0; i < pages; ++i) {
        DrawRectangle(x0 + i * spacing, APP_DOTS_Y, size, size, d->dim);
    }
    const float e = screens_ease_out(v->trans);
    const float pos = v->prev_screen + ((float)v->screen - (float)v->prev_screen) * e;
    DrawRectangle(x0 + (int)(pos * spacing) - 2, APP_DOTS_Y - 2, size + 4, size + 4, d->color);
}

void pet_view_draw(const pet_view_t *v, const app_game_snapshot_t *s)
{
    const px_grid_t grid = px_grid(s->pixel_style);
    /* Dimming is drawn, not commanded: the panel is never touched from the game
     * loop (see app_power_hw/README.md), and dark pixels cost little on AMOLED. */
    const float level = (s->power == APP_POWER_DIM ? DIM_LEVEL : 1.0f) * (float)s->brightness_pct / 100.0f;
    const Color c = px_scale(px_color(s->color_index), level);
    const draw_ctx_t d = {
        .view = v, .snap = s, .grid = grid, .gap = grid.gap > 0,
        .color = c, .accent = px_scale(px_accent(s->color_index), level), .dim = px_scale(c, 0.24f),
    };
    ClearBackground(BLACK);
    const bool panel = v->overlay_drawn != APP_OVERLAY_NONE;
    const bool covered = panel && v->overlay_pos >= 1.0f
                         && screens_overlay_height(v->overlay_drawn) >= APP_VIEW_H;
    if (!covered) {
        if (v->trans < 1.0f) {
            const float e = screens_ease_out(v->trans);
            screens_draw(&d, v->prev_screen, (int)(-v->trans_dir * APP_VIEW_W * e));
            screens_draw(&d, v->screen, (int)(v->trans_dir * APP_VIEW_W * (1.0f - e)));
        } else {
            screens_draw(&d, v->screen, 0);
        }
        draw_steps_counter(v, &d);
        draw_dots(v, &d);
    }
    if (panel) {
        const int height = screens_overlay_height(v->overlay_drawn);
        const int oy = -(int)(height * (1.0f - screens_ease_out(v->overlay_pos)));
        screens_draw_overlay(&d, v->overlay_drawn, oy);
    }
    /* Unlocked animations change how the eyes are drawn. */
    const bool rainbow_on = anim_on(s, APP_ANIM_RAINBOW) && s->mood == APP_MOOD_WALK;
    const bool stars = anim_on(s, APP_ANIM_STARS) && s->mood == APP_MOOD_HAPPY;
    const bool fainted = s->mood == APP_MOOD_FAINTED;
    pet_eyes_draw(&v->eyes, current_anchor(v), grid,
                  fainted ? px_scale(c, 0.55f) : (rainbow_on ? rainbow(v->t) : c), stars, fainted);
    draw_food_toss(v, &d);
    draw_particles(v, &d);
    draw_crt(v, &d);
    draw_sparks_counter(v, &d);
}
