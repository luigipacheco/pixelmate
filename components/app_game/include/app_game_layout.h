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
};

/* Eye placement per screen: center and scale relative to the home eyes. */
typedef struct {
    int16_t cx, cy;
    float scale;
} app_eye_anchor_t;

static inline app_eye_anchor_t app_eye_anchor(int screen)
{
    switch (screen) {
    case 0: return (app_eye_anchor_t) {240, 236, 1.0f};   /* home */
    case 2: return (app_eye_anchor_t) {240, 150, 0.62f};  /* feed */
    case 3: return (app_eye_anchor_t) {240, 104, 0.46f};  /* cards */
    case 4: return (app_eye_anchor_t) {240, 160, 0.72f};  /* settings */
    default: return (app_eye_anchor_t) {240, 236, 0.0f};  /* no eyes */
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

static inline app_rect_t app_settings_pixel_rect(int i)
{
    return (app_rect_t) {(int16_t)(24 + i * 148), 284, 136, 52};
}
static inline app_rect_t app_settings_color_rect(int i)
{
    return (app_rect_t) {(int16_t)(24 + i * 74), 372, 64, 56};
}
