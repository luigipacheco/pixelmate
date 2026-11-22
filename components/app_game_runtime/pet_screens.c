// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

#define TWO_PI 6.2831853f
static const Color INK = {12, 12, 12, 255};
static const Color CARD_FACE = {236, 236, 226, 255};
static const Color WARN = {255, 48, 48, 255};

enum {
    RING_CX = 240, RING_CY = 206, RING_OUT = 132, RING_IN = 106,
    CARD_W = 112, CARD_H = 160,
    BAR_SEGMENTS = 10,
};

float screens_ease_out(float t)
{
    if (t >= 1.0f) {
        return 1.0f;
    }
    if (t <= 0.0f) {
        return 0.0f;
    }
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

const char *screens_card_label(uint8_t value)
{
    static const char *const labels[14] = {
        "?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    return labels[value <= 13 ? value : 0];
}

/* ---- Steps ring ------------------------------------------------------ */

typedef struct {
    float cx, cy, r_in2, r_out2, limit;
} ring_shape_t;

static bool ring_inside(const void *shape, float x, float y)
{
    const ring_shape_t *r = shape;
    const float dx = x - r->cx, dy = y - r->cy;
    const float d2 = dx * dx + dy * dy;
    if (d2 < r->r_in2 || d2 > r->r_out2) {
        return false;
    }
    float angle = atan2f(dx, -dy);   /* 0 at 12 o'clock, clockwise. */
    if (angle < 0) {
        angle += TWO_PI;
    }
    return angle <= r->limit;
}

static void draw_ring(const draw_ctx_t *d, int ox, float progress, Color color)
{
    const ring_shape_t ring = {
        (float)(RING_CX + ox), (float)RING_CY,
        (float)(RING_IN * RING_IN), (float)(RING_OUT * RING_OUT), progress * TWO_PI
    };
    px_fill_shape(d->grid, ring.cx - RING_OUT, ring.cy - RING_OUT, ring.cx + RING_OUT + 1,
                  ring.cy + RING_OUT + 1, ring_inside, &ring, color);
}

static void draw_bar(const draw_ctx_t *d, int x, int y, int value, Color color)
{
    const int filled = (value + 9) / 10;
    for (int i = 0; i < BAR_SEGMENTS; ++i) {
        DrawRectangle(x + i * 22, y, 18, 18, i < filled ? color : d->dim);
    }
}

static void draw_steps(const draw_ctx_t *d, int ox)
{
    const pet_view_t *v = d->view;
    const app_game_snapshot_t *s = d->snap;
    draw_ring(d, ox, 1.0f, d->dim);
    if (v->ring_shown > 0.002f) {
        draw_ring(d, ox, v->ring_shown, s->steps_today >= s->step_goal ? d->accent : d->color);
    }
    char text[24];
    px_text_center("TODAY", RING_CX + ox, 142, 2, d->dim, false);
    snprintf(text, sizeof(text), "%lu", (unsigned long)(v->steps_shown + 0.5f));
    const int scale = px_text_width(text, 6) > 2 * RING_IN - 24 ? 5 : 6;
    px_text_center(text, RING_CX + ox, 176, scale, d->color, d->gap);
    snprintf(text, sizeof(text), "/%lu", (unsigned long)s->step_goal);
    px_text_center(text, RING_CX + ox, 236, 3, d->dim, d->gap);

    snprintf(text, sizeof(text), "TOTAL %lu", (unsigned long)s->steps_total);
    px_text_center(text, 240 + ox, 350, 3, d->color, d->gap);
    /* Health hearts: +1 for each day fed (half the goal), -1 for each missed. */
    px_text_center("HEALTH", 240 + ox, 384, 2, d->dim, false);
    for (int i = 0; i < APP_HP_MAX; ++i) {
        const bool full = i < s->hp;
        px_icon(full ? PX_ICON_HEART : PX_ICON_HEART_EMPTY, 240 + ox - 82 + i * 34, 404, 3,
                full ? d->accent : (s->hp == 0 ? WARN : d->dim), d->accent, d->gap);
    }
}

/* ---- Home ------------------------------------------------------------ */

/* Home is just the pet. The eyes carry the mood; text appears only when the
 * pet has fainted, because that is the one state that needs an instruction.
 * Progress and unlock hints live in the menu and on the Steps/Feed screens. */
static void draw_home(const draw_ctx_t *d, int ox)
{
    const app_game_snapshot_t *s = d->snap;
    if (s->overlay != APP_OVERLAY_NONE) {
        return;
    }
    if (s->fainted) {
        px_text_center("WALK TO REVIVE", 240 + ox, 402, 3, WARN, d->gap);
        return;
    }
    if (s->mood == APP_MOOD_HUNGRY && (d->view->t / 12U) % 2U == 0U) {
        px_icon(PX_ICON_BATTERY_LOW, 240 + ox - 14, 72, 4, d->color, WARN, d->gap);
    }
}

/* ---- Feed ------------------------------------------------------------ */

const char *screens_energy_word(uint8_t energy)
{
    if (energy < 15) {
        return "STARVING";
    }
    if (energy < 40) {
        return "HUNGRY";
    }
    return energy < 75 ? "PECKISH" : "FULL";
}

static void draw_feed(const draw_ctx_t *d, int ox)
{
    const app_game_snapshot_t *s = d->snap;
    /* Energy is a feeling, not a quota: a bar and a word, no step numbers. */
    px_text("ENERGY", 56 + ox, 218, 2, d->dim, false);
    draw_bar(d, 142 + ox, 214, s->hunger, s->hunger < 25 ? WARN : d->color);
    char text[32];
    px_text_center(screens_energy_word(s->hunger), 240 + ox, 242, 2,
                   s->hunger < 25 ? WARN : (s->hunger >= 75 ? d->accent : d->dim), false);
    for (int i = 0; i < APP_FOOD_COUNT; ++i) {
        const app_food_t *food = app_game_food(i);
        const app_rect_t r = app_feed_item_rect(i);
        const bool affordable = s->sparks >= food->cost && !s->fainted;
        const Color c = affordable ? d->color : d->dim;
        px_box(r.x + ox, r.y, r.w, r.h, 3, c);
        const char *const *icon = PX_ICON_FOOD[i];
        const int iw = px_icon_width(icon) * 6, ih = px_icon_height(icon) * 6;
        px_icon(icon, r.x + ox + (r.w - iw) / 2, r.y + 64 - ih / 2, 6, c,
                affordable ? d->accent : d->dim, d->gap);
        px_text_center(food->name, r.x + ox + r.w / 2, r.y + 112, 2, c, false);
        snprintf(text, sizeof(text), "%u", food->cost);
        const int tw = px_text_width(text, 3);
        const int x0 = r.x + ox + (r.w - tw - 18) / 2;
        px_icon(PX_ICON_BOLT, x0, r.y + 132, 2, affordable ? d->accent : d->dim, c, false);
        px_text(text, x0 + 18, r.y + 132, 3, c, d->gap);
    }
}

/* ---- Cards ----------------------------------------------------------- */

static void draw_card(const draw_ctx_t *d, int cx, int y, float width, bool face, uint8_t value)
{
    const int w = (int)(CARD_W * width);
    if (w < 4) {
        return;
    }
    const int x = cx - w / 2;
    if (face) {
        px_box(x, y, w, CARD_H, 0, CARD_FACE);
        if (width > 0.6f) {
            const char *label = screens_card_label(value);
            px_text_center(label, cx, y + CARD_H / 2 - 24, value == 10 ? 6 : 7, INK, d->gap);
            px_icon(PX_ICON_BOLT, x + 8, y + 8, 2, d->accent, d->accent, false);
            px_icon(PX_ICON_BOLT, x + w - 20, y + CARD_H - 22, 2, d->accent, d->accent, false);
        }
        return;
    }
    px_box(x, y, w, CARD_H, 0, px_scale(d->color, 0.16f));
    px_box(x, y, w, CARD_H, 4, d->color);
    if (width > 0.6f) {
        px_icon(PX_ICON_BOLT, cx - 18, y + CARD_H / 2 - 21, 6, d->color, d->color, d->gap);
    }
}

void screens_button(const draw_ctx_t *d, app_rect_t r, int ox, int oy, const char *label,
                    int scale, Color color, bool filled)
{
    if (filled) {
        px_box(r.x + ox, r.y + oy, r.w, r.h, 0, color);
    } else {
        px_box(r.x + ox, r.y + oy, r.w, r.h, 3, color);
    }
    px_text_center(label, r.x + ox + r.w / 2, r.y + oy + (r.h - 7 * scale) / 2, scale,
                   filled ? INK : color, d->gap);
}

static void draw_button(const draw_ctx_t *d, app_rect_t r, int ox, const char *label,
                        int scale, Color color, bool filled)
{
    screens_button(d, r, ox, 0, label, scale, color, filled);
}

static void draw_cards(const draw_ctx_t *d, int ox)
{
    const pet_view_t *v = d->view;
    const app_cards_snapshot_t *c = &d->snap->cards;
    char text[20];
    if (c->phase == APP_CARDS_READY) {
        snprintf(text, sizeof(text), "BEST %u", c->best_streak);
        px_text_center(text, 240 + ox, 146, 3, d->dim, d->gap);
    } else {
        snprintf(text, sizeof(text), "POT %u", c->pot);
        px_text_center(text, 240 + ox, 146, 3, d->color, d->gap);
    }

    const app_rect_t a = app_cards_slot_rect(0), b = app_cards_slot_rect(1);
    const float deal = screens_ease_out(v->deal_t);
    const float slide = screens_ease_out(v->slide_t);
    const int deal_off = (int)((1.0f - deal) * 340.0f);
    const int a_cx = a.x + CARD_W / 2, b_cx = b.x + CARD_W / 2;
    /* Current card: slides from the right slot after a win/tie. */
    const int cur_cx = slide < 1.0f ? b_cx + (int)((a_cx - b_cx) * slide) : a_cx;
    draw_card(d, cur_cx + ox + deal_off, a.y, 1.0f, true, c->current);
    /* Next card: face down, flips on reveal; a fresh one slides in after a win. */
    if (c->phase == APP_CARDS_READY) {
        px_box(b.x + ox, b.y, CARD_W, CARD_H, 3, d->dim);
    } else {
        float width = 1.0f;
        bool face = false;
        if (c->phase == APP_CARDS_REVEAL) {
            width = fabsf(cosf(v->flip_t < 1.0f ? v->flip_t * 3.14159f : 3.14159f));
            face = v->flip_t >= 0.5f;
        }
        const int in_off = slide < 1.0f ? (int)((1.0f - slide) * 260.0f) : 0;
        draw_card(d, b_cx + ox + deal_off + in_off, b.y, width, face, c->next);
    }

    if (c->phase == APP_CARDS_READY) {
        const bool can = d->snap->sparks >= APP_CARDS_DEAL_COST;
        snprintf(text, sizeof(text), "DEAL %d", APP_CARDS_DEAL_COST);
        draw_button(d, app_cards_deal_rect(), ox, text, 4, can ? d->color : d->dim, can);
        return;
    }
    const bool guessing = c->phase == APP_CARDS_GUESS;
    draw_button(d, app_cards_hi_rect(), ox, "HI", 5, guessing ? d->color : d->dim, false);
    draw_button(d, app_cards_lo_rect(), ox, "LO", 5, guessing ? d->accent : d->dim, false);
    const bool can_cash = guessing && c->streak > 0;
    draw_button(d, app_cards_cash_rect(), ox, "CASH", 3, can_cash ? d->color : d->dim, can_cash);
}

/* ---- Spark Slots ----------------------------------------------------- */

void screens_slot_symbol(const draw_ctx_t *d, uint8_t symbol, int cx, int cy, int scale_pct)
{
    const int k = scale_pct;   /* Percent of full size, for small previews. */
    switch ((app_slot_symbol_t)symbol) {
    case APP_SLOT_SEVEN: {
        const int s = 7 * k / 100 > 1 ? 7 * k / 100 : 1;
        px_text_center("7", cx, cy - 7 * s / 2, s, d->accent, d->gap);
        return;
    }
    case APP_SLOT_STAR: {
        const int s = 6 * k / 100 > 1 ? 6 * k / 100 : 1;
        px_icon(PX_ICON_STAR, cx - 7 * s / 2, cy - 4 * s, s, d->color, d->accent, d->gap);
        return;
    }
    case APP_SLOT_EYE: {
        const int s = 4 * k / 100 > 1 ? 4 * k / 100 : 1;
        px_icon(PX_ICON_EYE, cx - 6 * s, cy - 3 * s, s, d->color, d->accent, d->gap);
        return;
    }
    case APP_SLOT_BOLT: {
        const int s = 6 * k / 100 > 1 ? 6 * k / 100 : 1;
        px_icon(PX_ICON_BOLT, cx - 3 * s, cy - 7 * s / 2, s, d->color, d->color, d->gap);
        return;
    }
    case APP_SLOT_HEART: {
        const int s = 6 * k / 100 > 1 ? 6 * k / 100 : 1;
        px_icon(PX_ICON_HEART, cx - 7 * s / 2, cy - 3 * s, s, d->accent, d->accent, d->gap);
        return;
    }
    default: {
        const int s = 5 * k / 100 > 1 ? 5 * k / 100 : 1;
        px_icon(PX_ICON_SHOE, cx - 4 * s, cy - 3 * s, s, d->color, d->accent, d->gap);
        return;
    }
    }
}

static void draw_reel(const draw_ctx_t *d, int reel, int ox)
{
    const app_slots_snapshot_t *sl = &d->snap->slots;
    const app_rect_t w = app_slots_window_rect(reel);
    const int x = w.x + ox, cx = x + w.w / 2, cy = w.y + w.h / 2;
    const int frac = sl->pos_q[reel] % APP_SLOT_Q;
    const bool win_blink = sl->phase == APP_SLOTS_RESULT && sl->payout > 0 && (d->view->t / 6U) % 2U;
    for (int row = -2; row <= 2; ++row) {
        const int y = cy - row * APP_SLOT_SYMBOL_H + frac * APP_SLOT_SYMBOL_H / APP_SLOT_Q;
        if (row == 0 && frac == 0 && win_blink) {
            continue;
        }
        screens_slot_symbol(d, app_game_slot_symbol(sl->pos_q[reel], row), cx, y, 100);
    }
    /* Mask symbols that scrolled past the window edges. */
    DrawRectangle(x - 6, w.y - 76, w.w + 12, 72, BLACK);
    DrawRectangle(x - 6, w.y + w.h + 4, w.w + 12, 72, BLACK);
    const bool stopped = sl->phase == APP_SLOTS_SPINNING && (sl->stopped & (1U << reel));
    px_box(x - 6, w.y - 6, w.w + 12, w.h + 12, 3, stopped ? d->accent : d->color);
}

static void draw_slots(const draw_ctx_t *d, int ox)
{
    const app_game_snapshot_t *s = d->snap;
    const app_slots_snapshot_t *sl = &s->slots;
    for (int reel = 0; reel < APP_SLOT_REELS; ++reel) {
        draw_reel(d, reel, ox);
    }
    const app_rect_t first = app_slots_window_rect(0);
    const int line_y = first.y + first.h / 2 - 4;
    DrawRectangle(ox + 24, line_y, 18, 8, d->accent);
    DrawRectangle(ox + APP_VIEW_W - 42, line_y, 18, 8, d->accent);

    char text[24];
    if (sl->phase == APP_SLOTS_SPINNING) {
        px_text_center("TAP TO STOP", 240 + ox, 340, 2, d->dim, false);
        screens_button(d, app_slots_button_rect(), ox, 0, "STOP", 4, d->accent, true);
        return;
    }
    if (sl->phase == APP_SLOTS_RESULT) {
        if (sl->payout) {
            snprintf(text, sizeof(text), "WIN %u", sl->payout);
            px_text_center(text, 240 + ox, 336, 3, d->accent, d->gap);
        } else {
            px_text_center("NO LUCK", 240 + ox, 336, 3, d->dim, d->gap);
        }
    } else {
        snprintf(text, sizeof(text), "777 PAYS %u", app_game_slot_triple_pay(APP_SLOT_SEVEN));
        px_text_center(text, 240 + ox, 340, 2, d->dim, false);
    }
    const bool can = s->sparks >= APP_SLOTS_BET;
    snprintf(text, sizeof(text), "SPIN %d", APP_SLOTS_BET);
    screens_button(d, app_slots_button_rect(), ox, 0, text, 4, can ? d->color : d->dim, can);
}

void screens_draw(const draw_ctx_t *d, app_screen_t screen, int ox)
{
    if (ox <= -APP_VIEW_W || ox >= APP_VIEW_W) {
        return;
    }
    switch (screen) {
    case APP_SCREEN_HOME: draw_home(d, ox); break;
    case APP_SCREEN_STEPS: draw_steps(d, ox); break;
    case APP_SCREEN_FEED: draw_feed(d, ox); break;
    case APP_SCREEN_ARCADE:
        switch (d->snap->arcade) {
        case APP_ARCADE_CARDS: draw_cards(d, ox); screens_draw_exit(d, ox); break;
        case APP_ARCADE_SLOTS: draw_slots(d, ox); screens_draw_exit(d, ox); break;
        case APP_ARCADE_GRID: screens_draw_grid(d, ox); screens_draw_exit(d, ox); break;
        case APP_ARCADE_BITS: screens_draw_bits(d, ox); screens_draw_exit(d, ox); break;
        case APP_ARCADE_SAYS: screens_draw_says(d, ox); screens_draw_exit(d, ox); break;
        case APP_ARCADE_STACK: screens_draw_stack(d, ox); screens_draw_exit(d, ox); break;
        default: screens_draw_arcade(d, ox); break;
        }
        break;
    case APP_SCREEN_BOOTH: screens_draw_booth(d, ox); break;
    default: break;
    }
}
