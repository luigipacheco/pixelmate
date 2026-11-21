// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Screen geometry shared by the model's hit tests and the view's drawing, so a
 * button is always drawn where it is tapped. Units are pixels on the 480x480 panel. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t x, y, w, h;
} app_rect_t;

static inline bool app_rect_hit(app_rect_t r, int32_t px, int32_t py)
{
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

enum {
    APP_VIEW_W = 480,
    APP_VIEW_H = 480,
    APP_TOP_BAR_H = 56,
    APP_DOTS_Y = 452,
    APP_SWIPE_MIN_PX = 70,    /**< Horizontal travel that turns a drag into a swipe. */
    APP_TAP_SLOP_PX = 28,     /**< Movement still accepted as a tap. */
    APP_PULL_ZONE_H = 90,     /**< A downward drag starting above this opens the menu. */
    APP_PULL_MIN_PX = 70,     /**< Vertical travel that opens/closes a panel. */
    APP_MENU_H = 362,         /**< Height of the pull-down menu panel. */
};

/* Eye placement: center and scale relative to the home eyes (0 = hidden). */
typedef struct {
    int16_t cx, cy;
    float scale;
} app_eye_anchor_t;

static inline app_eye_anchor_t app_eye_anchor(int screen, int overlay, int arcade)
{
    switch (overlay) {
    case 1: return (app_eye_anchor_t) {240, 412, 0.4f};    /* menu: peeking below */
    case 2: return (app_eye_anchor_t) {240, 160, 0.72f};   /* style preview */
    case 3: return (app_eye_anchor_t) {240, 140, 0.0f};    /* store: hidden */
    case 4: return (app_eye_anchor_t) {240, 400, 0.42f};   /* goal: watching the number */
    default: break;
    }
    switch (screen) {
    case 0: return (app_eye_anchor_t) {240, 236, 1.0f};    /* home */
    case 2: return (app_eye_anchor_t) {240, 150, 0.62f};   /* feed */
    case 3:                                                 /* arcade */
        switch (arcade) {
        case 1: return (app_eye_anchor_t) {240, 104, 0.46f};   /* HI-LO */
        case 2: return (app_eye_anchor_t) {240, 98, 0.42f};    /* slots */
        case 3: return (app_eye_anchor_t) {426, 340, 0.26f};   /* grid: watching from the side */
        default: return (app_eye_anchor_t) {240, 98, 0.42f};   /* lobby: watching */
        }
    default: return (app_eye_anchor_t) {240, 206, 0.0f};   /* steps: no eyes */
    }
}

/* Arcade lobby: three cabinets side by side. */
static inline app_rect_t app_arcade_cabinet_rect(int i)
{
    return (app_rect_t) {(int16_t)(16 + i * 152), 152, 144, 280};
}
/* Leaves a game, back to the cabinets (left of the eyes, under the top bar). */
static inline app_rect_t app_arcade_exit_rect(void)
{
    return (app_rect_t) {12, 72, 104, 44};
}

static inline app_rect_t app_home_eyes_rect(void)
{
    return (app_rect_t) {80, 120, 320, 240};
}

static inline app_rect_t app_feed_item_rect(int i)
{
    return (app_rect_t) {(int16_t)(24 + i * 148), 266, 136, 160};
}

static inline app_rect_t app_cards_deal_rect(void)
{
    return (app_rect_t) {130, 362, 220, 64};
}
static inline app_rect_t app_cards_hi_rect(void)
{
    return (app_rect_t) {24, 362, 140, 64};
}
static inline app_rect_t app_cards_cash_rect(void)
{
    return (app_rect_t) {176, 362, 128, 64};
}
static inline app_rect_t app_cards_lo_rect(void)
{
    return (app_rect_t) {316, 362, 140, 64};
}
static inline app_rect_t app_cards_slot_rect(int slot)
{
    return (app_rect_t) {(int16_t)(slot ? 256 : 112), 176, 112, 160};
}

/* Spark Slots: three reel windows and the SPIN/STOP button. */
enum { APP_SLOT_WINDOW_W = 112, APP_SLOT_WINDOW_H = 160, APP_SLOT_SYMBOL_H = 64 };
static inline app_rect_t app_slots_window_rect(int reel)
{
    return (app_rect_t) {(int16_t)(56 + reel * 128), 160, APP_SLOT_WINDOW_W, APP_SLOT_WINDOW_H};
}
static inline app_rect_t app_slots_reels_rect(void)
{
    return (app_rect_t) {48, 152, 384, 176};
}
static inline app_rect_t app_slots_button_rect(void)
{
    return (app_rect_t) {130, 372, 220, 64};
}

/* Spark Grid: 5x5 tiles, a hint column/row, and a PLAY/CASH button. */
enum { APP_GRID_CELL = 52, APP_GRID_X = 52, APP_GRID_Y = 126 };
static inline app_rect_t app_grid_tile_rect(int tile)
{
    return (app_rect_t) {(int16_t)(APP_GRID_X + (tile % 5) * APP_GRID_CELL),
                         (int16_t)(APP_GRID_Y + (tile / 5) * APP_GRID_CELL), APP_GRID_CELL, APP_GRID_CELL};
}
static inline app_rect_t app_grid_button_rect(void)
{
    return (app_rect_t) {382, 126, 88, 64};
}

/* Pull-down menu entries (inside the APP_MENU_H panel). The top row holds the
 * battery readout (left), SLEEP (center) and the spark counter (right). */
static inline app_rect_t app_menu_sleep_rect(void)
{
    return (app_rect_t) {176, 8, 128, 44};
}
static inline app_rect_t app_menu_store_rect(void)
{
    return (app_rect_t) {40, 64, 400, 66};
}
static inline app_rect_t app_menu_style_rect(void)
{
    return (app_rect_t) {40, 142, 400, 66};
}
static inline app_rect_t app_menu_goal_rect(void)
{
    return (app_rect_t) {40, 220, 400, 66};
}

/* Daily goal panel. */
static inline app_rect_t app_goal_minus_rect(void)
{
    return (app_rect_t) {40, 222, 150, 84};
}
static inline app_rect_t app_goal_plus_rect(void)
{
    return (app_rect_t) {290, 222, 150, 84};
}

/* Shared by the style and store panels. */
static inline app_rect_t app_panel_back_rect(void)
{
    return (app_rect_t) {12, 8, 116, 44};
}

static inline app_rect_t app_style_pixel_rect(int i)
{
    return (app_rect_t) {(int16_t)(24 + i * 148), 284, 136, 52};
}
static inline app_rect_t app_style_color_rect(int i)
{
    return (app_rect_t) {(int16_t)(24 + i * 74), 372, 64, 56};
}
/* SOUND ON / LOUD / OFF, left of the eye preview. */
static inline app_rect_t app_style_sound_rect(void)
{
    return (app_rect_t) {12, 96, 100, 116};
}

static inline app_rect_t app_store_row_rect(int row)
{
    return (app_rect_t) {16, (int16_t)(68 + row * 92), 448, 84};
}
