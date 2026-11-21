// SPDX-License-Identifier: Apache-2.0
/* Arcade lobby: a row of pixel cabinets, each running an attract-mode demo of
 * its game on a tiny screen. Games themselves draw in pet_screens.c. */
#include <math.h>
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color INK = {12, 12, 12, 255};
static const Color SCREEN_BG = {6, 10, 4, 255};
static const Color CARD_FACE = {236, 236, 226, 255};

enum {
    MARQUEE_H = 34,
    SCREEN_INSET = 14,
    SCREEN_TOP = 48,
    SCREEN_H = 104,
    PANEL_TOP = 166,
    COIN_TOP = 222,
    ATTRACT_PERIOD = 90,   /* Ticks per attract-mode loop. */
};

static bool cabinet_unlocked(const app_game_snapshot_t *s, app_arcade_t game)
{
    switch (game) {
    case APP_ARCADE_CARDS: return s->owned & APP_OWN_GAME_BIT(APP_GAME_CARDS);
    case APP_ARCADE_SLOTS: return s->owned & APP_OWN_GAME_BIT(APP_GAME_SLOTS);
    case APP_ARCADE_GRID: return s->owned & APP_OWN_GAME_BIT(APP_GAME_GRID);
    case APP_ARCADE_BITS: return s->owned & APP_OWN_GAME_BIT(APP_GAME_BITS);
    default: return false;
    }
}

static const char *cabinet_name(app_arcade_t game)
{
    switch (game) {
    case APP_ARCADE_CARDS: return "HI-LO";
    case APP_ARCADE_SLOTS: return "SLOTS";
    case APP_ARCADE_GRID: return "GRID";
    default: return "BITS";
    }
}

/* Mini screens ---------------------------------------------------------- */

static void attract_cards(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    /* Left card face up; right card flips over every loop. */
    const int cw = 34, ch = 50, cy = y + (SCREEN_H - ch) / 2;
    px_box(x + w / 2 - cw - 6, cy, cw, ch, 0, CARD_FACE);
    px_text_center("7", x + w / 2 - cw / 2 - 6, cy + 15, 3, INK, false);
    const float phase = (float)(t % ATTRACT_PERIOD) / ATTRACT_PERIOD;
    const float width = fabsf(cosf(phase * 3.14159f));
    const int fw = (int)(cw * width);
    const int fx = x + w / 2 + 6 + (cw - fw) / 2;
    if (fw < 3) {
        return;
    }
    if (phase < 0.5f) {
        px_box(fx, cy, fw, ch, 0, px_scale(d->color, 0.2f));
        px_box(fx, cy, fw, ch, 2, d->color);
    } else {
        static const char *const reveal[] = {"K", "2", "J", "9"};
        px_box(fx, cy, fw, ch, 0, CARD_FACE);
        if (width > 0.6f) {
            px_text_center(reveal[(t / ATTRACT_PERIOD) % 4U], fx + fw / 2, cy + 15, 3, INK, false);
        }
    }
}

static void attract_slots(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    /* Three reels ticking past; every loop they land on 7-7-7. */
    const int rw = (w - 16) / 3;
    const uint32_t phase = t % ATTRACT_PERIOD;
    for (int r = 0; r < APP_SLOT_REELS; ++r) {
        const int rx = x + 6 + r * (rw + 2);
        px_box(rx, y + 8, rw, SCREEN_H - 16, 2, px_scale(d->color, 0.5f));
        const bool landed = phase > 40U + (uint32_t)r * 12U;
        const uint8_t symbol = landed ? APP_SLOT_SEVEN
                               : app_game_slot_symbol((int)((t * 7U + r * 40U) % (APP_SLOT_STRIP * APP_SLOT_Q)), 0);
        screens_slot_symbol(d, symbol, rx + rw / 2, y + SCREEN_H / 2, 55);
    }
}

static void attract_bits(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    /* Counts up in binary: the number on top, eight tiny levers below. */
    const uint8_t value = (uint8_t)((t / 8U) % 256U);
    char text[4];
    snprintf(text, sizeof(text), "%u", value);
    px_text_center(text, x + w / 2, y + 14, 4, d->color, false);
    const int cell = (w - 8) / 8;
    for (int i = 0; i < 8; ++i) {
        const bool on = value & (0x80U >> i);
        const int bx = x + 4 + i * cell;
        px_box(bx + 1, y + 54, cell - 2, 36, 1, on ? d->color : px_scale(d->color, 0.35f));
        DrawRectangle(bx + 3, on ? y + 57 : y + 73, cell - 6, 14, on ? d->accent : px_scale(d->color, 0.2f));
    }
}

static void attract_grid(const draw_ctx_t *d, int x, int y, int w, uint32_t t)
{
    /* A 3x3 board turning tiles one by one; the last one is a dud. */
    static const uint8_t faces[9] = {2, 1, 3, 1, 0, 2, 1, 1, 3};
    static const uint8_t order[9] = {0, 4, 2, 6, 8, 1, 3, 7, 5};
    const int cell = 26, gx = x + (w - 3 * cell) / 2, gy = y + (SCREEN_H - 3 * cell) / 2;
    const uint32_t shown = (t % ATTRACT_PERIOD) / 9U;
    for (int i = 0; i < 9; ++i) {
        int k = 0;
        while (order[k] != i) {
            ++k;
        }
        const int tx = gx + (i % 3) * cell, ty = gy + (i / 3) * cell;
        if ((uint32_t)k >= shown) {
            px_box(tx + 2, ty + 2, cell - 4, cell - 4, 1, d->color);
            continue;
        }
        const uint8_t v = faces[i];
        px_box(tx + 2, ty + 2, cell - 4, cell - 4, 0, v ? CARD_FACE : (Color){255, 48, 48, 255});
        if (v) {
            char label[2] = {(char)('0' + v), 0};
            px_text_center(label, tx + cell / 2, ty + 7, 2, v > 1 ? d->accent : INK, false);
        }
    }
}

/* Cabinet ---------------------------------------------------------------- */

static void draw_controls(int x, int y, int w, Color c, Color accent)
{
    /* Sloped control deck with a joystick and two buttons. */
    DrawRectangle(x + 4, y, w - 8, 6, c);
    DrawRectangle(x, y + 6, w, 30, px_scale(c, 0.35f));
    DrawRectangle(x + 26, y - 8, 5, 18, c);                 /* stick */
    px_box(x + 21, y - 16, 15, 12, 0, accent);              /* ball */
    px_box(x + w - 58, y + 12, 16, 12, 0, accent);          /* buttons */
    px_box(x + w - 34, y + 12, 16, 12, 0, c);
}

static void draw_cabinet(const draw_ctx_t *d, int index, int ox, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    const app_arcade_t game = (app_arcade_t)(index + 1);
    const app_rect_t r = app_arcade_cabinet_rect(index % APP_ARCADE_PER_ROW);
    const bool unlocked = cabinet_unlocked(s, game);
    const Color c = unlocked ? d->color : px_scale(d->color, 0.35f);
    const Color accent = unlocked ? d->accent : px_scale(d->accent, 0.35f);
    const int x = r.x + ox, y = r.y + oy;
    const uint32_t t = d->view->t + (uint32_t)index * 23U;   /* Offset so they don't sync. */

    /* Marquee with chasing lights along its bottom edge. */
    px_box(x, y, r.w, MARQUEE_H, 0, c);
    px_text_center(cabinet_name(game), x + r.w / 2, y + 8, 3, INK, false);
    for (int i = 0; i < 9; ++i) {
        const bool lit = unlocked && (((t / 6U) + (uint32_t)i) % 3U == 0U);
        DrawRectangle(x + 8 + i * 15, y + MARQUEE_H - 6, 6, 4, lit ? accent : INK);
    }
    /* Body, screen bezel and screen. */
    px_box(x, y + MARQUEE_H + 2, r.w, r.h - MARQUEE_H - 2, 3, c);
    const int sx = x + SCREEN_INSET, sy = y + SCREEN_TOP, sw = r.w - 2 * SCREEN_INSET;
    DrawRectangle(sx - 3, sy - 3, sw + 6, SCREEN_H + 6, px_scale(c, 0.3f));
    DrawRectangle(sx, sy, sw, SCREEN_H, SCREEN_BG);
    if (unlocked) {
        if (game == APP_ARCADE_CARDS) {
            attract_cards(d, sx, sy, sw, t);
        } else if (game == APP_ARCADE_SLOTS) {
            attract_slots(d, sx, sy, sw, t);
        } else if (game == APP_ARCADE_GRID) {
            attract_grid(d, sx, sy, sw, t);
        } else {
            attract_bits(d, sx, sy, sw, t);
        }
    } else {
        px_icon(PX_ICON_LOCK, sx + sw / 2 - 14, sy + SCREEN_H / 2 - 16, 4, c, c, d->gap);
    }
    draw_controls(x + 10, y + PANEL_TOP, r.w - 20, c, accent);

    /* Coin slot: PLAY / INSERT COIN blink, or the price of a locked machine. */
    const int cy = y + COIN_TOP;
    DrawRectangle(x + r.w / 2 - 3, cy, 6, 16, px_scale(c, 0.6f));
    char text[12];
    if (unlocked) {
        const bool blink = (t / 20U) % 2U;
        px_text_center(blink ? "PLAY" : "INSERT", x + r.w / 2, cy + 26, 2, blink ? accent : c, false);
        if (!blink) {
            px_text_center("COIN", x + r.w / 2, cy + 42, 2, c, false);
        }
    } else {
        const app_minigame_t id = game == APP_ARCADE_GRID ? APP_GAME_GRID
                                  : game == APP_ARCADE_BITS ? APP_GAME_BITS
                                  : game == APP_ARCADE_CARDS ? APP_GAME_CARDS : APP_GAME_SLOTS;
        const app_store_item_t *item = app_game_store_item(app_game_store_find(APP_ITEM_GAME, id));
        snprintf(text, sizeof(text), "%u", item ? item->cost : 0);
        const int tw = px_text_width(text, 2);
        px_icon(PX_ICON_BOLT, x + (r.w - tw) / 2 - 14, cy + 26, 2, accent, accent, false);
        px_text(text, x + (r.w - tw) / 2 + 2, cy + 26, 2, s->sparks >= (item ? item->cost : 0) ? d->color : c, false);
    }
}

enum { ROW_PITCH = 300 };

static void draw_chevron(int cx, int y, bool down, Color c)
{
    for (int i = 0; i < 4; ++i) {
        const int dy = down ? i : 3 - i;
        DrawRectangle(cx - 12 + i * 3, y + dy * 3, 4, 4, c);
        DrawRectangle(cx + 8 - i * 3, y + dy * 3, 4, 4, c);
    }
}

void screens_draw_arcade(const draw_ctx_t *d, int ox)
{
    /* Rows of three; the view eases arcade_scroll toward the chosen row. */
    const float scroll = d->view->arcade_scroll;
    for (int i = 0; i < APP_ARCADE_CABINETS; ++i) {
        const int oy = (int)((i / APP_ARCADE_PER_ROW - scroll) * ROW_PITCH);
        if (oy > -ROW_PITCH && oy < ROW_PITCH) {
            draw_cabinet(d, i, ox, oy);
        }
    }
    /* Hide rows scrolled out of the cabinet band (top bar and dots draw later). */
    DrawRectangle(ox, 0, APP_VIEW_W, 148, BLACK);
    DrawRectangle(ox, 434, APP_VIEW_W, APP_VIEW_H - 434, BLACK);
    /* Blinking hints that more cabinets sit above or below. */
    const bool blink = (d->view->t / 15U) % 2U;
    const int row = d->snap->arcade_row;
    if (row + 1 < APP_ARCADE_ROWS) {
        draw_chevron(240 + ox, 436, true, blink ? d->accent : d->dim);
    }
    if (row > 0) {
        draw_chevron(240 + ox, 136, false, blink ? d->accent : d->dim);
    }
}

void screens_draw_exit(const draw_ctx_t *d, int ox)
{
    screens_button(d, app_arcade_exit_rect(), ox, 0, "EXIT", 3, d->dim, false);
}
