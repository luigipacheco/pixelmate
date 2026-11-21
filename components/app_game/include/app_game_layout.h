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
    APP_MENU_H = 290,         /**< Height of the pull-down menu panel. */
};

/* Eye placement: center and scale relative to the home eyes (0 = hidden). */
typedef struct {
    int16_t cx, cy;
    float scale;
} app_eye_anchor_t;

static inline app_eye_anchor_t app_eye_anchor(int screen, int overlay)
{
    switch (overlay) {
    case 1: return (app_eye_anchor_t) {240, 388, 0.5f};    /* menu: peeking below */
    case 2: return (app_eye_anchor_t) {240, 160, 0.72f};   /* style preview */
    case 3: return (app_eye_anchor_t) {240, 140, 0.0f};    /* store: hidden */
    default: break;
    }
    switch (screen) {
    case 0: return (app_eye_anchor_t) {240, 236, 1.0f};    /* home */
    case 2: return (app_eye_anchor_t) {240, 150, 0.62f};   /* feed */
    case 3: return (app_eye_anchor_t) {240, 104, 0.46f};   /* cards */
    default: return (app_eye_anchor_t) {240, 206, 0.0f};   /* steps: no eyes */
    }
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

/* Pull-down menu entries (inside the APP_MENU_H panel). */
static inline app_rect_t app_menu_store_rect(void)
{
    return (app_rect_t) {40, 76, 400, 76};
}
static inline app_rect_t app_menu_style_rect(void)
{
    return (app_rect_t) {40, 168, 400, 76};
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

static inline app_rect_t app_store_row_rect(int row)
{
    return (app_rect_t) {16, (int16_t)(68 + row * 92), 448, 84};
}
