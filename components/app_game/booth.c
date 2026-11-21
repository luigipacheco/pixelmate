// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "private/booth.h"
#include "include/app_game_layout.h"

enum {
    COUNT_TICKS = 20,          /* Per countdown digit (~0.7 s). */
    COUNT_FROM = 3,
    FLASH_TICKS = 8,
    GIF_FRAME_TICKS = 4,       /* 7.5 frames per second while recording. */
    REVIEW_TICKS = 75,
};

/* Every visible booth change bumps view.seq; the view diffs it, so the booth
 * needs no event bit of its own (all 32 are taken on main). Returns no bits. */
static uint32_t bump(booth_t *b)
{
    ++b->view.seq;
    return 0;
}

static uint8_t items(const booth_t *b)
{
    return (uint8_t)(b->view.photos + b->view.gifs);
}

static void set_phase(booth_t *b, app_booth_phase_t phase)
{
    b->view.phase = phase;
    b->view.phase_ticks = 0;
    b->view.confirm_delete = false;
}

static void request(booth_t *b, app_booth_request_kind_t kind, uint8_t frame, uint8_t index)
{
    b->request = (app_booth_request_t) {
        kind, b->view.filter, b->view.res,
        b->view.color && app_photo_filter_has_color(b->view.filter), frame, index
    };
}

void booth_init(booth_t *b)
{
    memset(b, 0, sizeof(*b));
}

void booth_enter(booth_t *b)
{
    set_phase(b, APP_BOOTH_LIVE);
    b->request.kind = APP_BOOTH_REQ_NONE;
}

void booth_cancel(booth_t *b)
{
    if (b->view.phase != APP_BOOTH_LIVE && b->view.phase != APP_BOOTH_GALLERY) {
        set_phase(b, APP_BOOTH_LIVE);
    }
    b->view.confirm_delete = false;
    b->view.on_air = false;   /* Leaving the page (or unplugging) ends a stream. */
    if (b->request.kind == APP_BOOTH_REQ_GIF_FRAME) {
        b->request.kind = APP_BOOTH_REQ_NONE;   /* A partial GIF is never saved. */
    }
}

void booth_select_filter(booth_t *b, uint8_t filter)
{
    b->view.filter = filter < APP_FILTER_COUNT ? filter : APP_FILTER_NONE;
}

void booth_select_lens(booth_t *b, uint8_t res)
{
    b->view.res = res < APP_PHOTO_RES_COUNT ? res : APP_PHOTO_RES_LOW;
}

/* Next owned entry of `count` in `dir` (+1/-1), wrapping; entry 0 is free. */
static uint8_t step_owned(uint8_t current, int dir, uint8_t owned, int count)
{
    int f = current;
    for (int i = 0; i < count; ++i) {
        f = (f + dir + count) % count;
        if (f == 0 || (owned & (1U << f))) {
            return (uint8_t)f;
        }
    }
    return current;
}

void booth_select_color(booth_t *b, bool color)
{
    b->view.color = color;
}

static uint32_t start_capture(booth_t *b, bool gif, bool fainted)
{
    if (fainted || !b->view.camera) {
        return APP_EVENT_BLOCKED;   /* Shakes the eyes; the view says why. */
    }
    const bool full = gif ? b->view.gifs >= APP_BOOTH_GIFS : b->view.photos >= APP_BOOTH_PHOTOS;
    b->view.gif = gif;
    if (full) {
        set_phase(b, APP_BOOTH_REVIEW);
        b->view.result = APP_BOOTH_RESULT_FULL;
        return bump(b);
    }
    set_phase(b, APP_BOOTH_COUNTDOWN);
    b->view.count = COUNT_FROM;
    b->view.frame = 0;
    b->view.result = APP_BOOTH_RESULT_NONE;
    return bump(b);
}

static uint32_t tap_live(booth_t *b, int32_t x, int32_t y, uint8_t looks, uint8_t lenses,
                         bool film_owned, bool fainted, uint16_t code)
{
    if (app_rect_hit(app_booth_air_rect(), x, y)) {
        if (!(lenses & APP_OWN_WEBCAM_BIT)) {
            return bump(b);   /* No WEBCAM yet: the view hints at the store. */
        }
        b->view.on_air = !b->view.on_air;
        if (b->view.on_air) {
            b->view.cam_code = code;
        }
        return bump(b);
    }
    if (app_rect_hit(app_booth_film_rect(), x, y)) {
        if (!film_owned) {
            return bump(b);   /* No COLOR FILM yet: the view hints at the store. */
        }
        b->view.color = !b->view.color;
        return APP_EVENT_SETTINGS | bump(b);
    }
    const int dir = app_rect_hit(app_booth_prev_rect(), x, y) ? -1
                    : (app_rect_hit(app_booth_next_rect(), x, y) ? 1 : 0);
    if (dir) {
        const uint8_t next = step_owned(b->view.filter, dir, looks, APP_FILTER_COUNT);
        if (next == b->view.filter) {
            return bump(b);   /* Nothing else owned: the view hints at the store. */
        }
        b->view.filter = next;
        return APP_EVENT_SETTINGS | bump(b);
    }
    if (app_rect_hit(app_booth_lens_rect(), x, y)) {
        const uint8_t next = step_owned(b->view.res, 1, lenses, APP_PHOTO_RES_COUNT);
        if (next == b->view.res) {
            return bump(b);
        }
        b->view.res = next;
        return APP_EVENT_SETTINGS | bump(b);
    }
    if (app_rect_hit(app_booth_button_rect(0), x, y)) {
        return start_capture(b, true, fainted);
    }
    if (app_rect_hit(app_booth_button_rect(1), x, y)) {
        return start_capture(b, false, fainted);
    }
    if (app_rect_hit(app_booth_button_rect(2), x, y)) {
        set_phase(b, APP_BOOTH_GALLERY);
        b->view.index = 0;
        return bump(b);
    }
    return 0;
}

static uint32_t tap_gallery(booth_t *b, int32_t x, int32_t y)
{
    const uint8_t count = items(b);
    if (app_rect_hit(app_booth_button_rect(0), x, y)) {
        set_phase(b, APP_BOOTH_LIVE);
        return bump(b);
    }
    if (count && app_rect_hit(app_booth_button_rect(2), x, y)) {
        if (!b->view.confirm_delete) {
            b->view.confirm_delete = true;
            return bump(b);
        }
        b->view.confirm_delete = false;
        request(b, APP_BOOTH_REQ_DELETE, 0, b->view.index);
        return bump(b);
    }
    b->view.confirm_delete = false;
    const int dir = app_rect_hit(app_booth_prev_rect(), x, y) ? -1
                    : (app_rect_hit(app_booth_next_rect(), x, y) ? 1 : 0);
    if (dir && count > 1) {
        b->view.index = (uint8_t)((b->view.index + dir + count) % count);
        return bump(b);
    }
    return 0;
}

uint32_t booth_tap(booth_t *b, int32_t x, int32_t y, uint8_t filters_owned, uint8_t lenses_owned,
                   bool film_owned, bool fainted, uint16_t code)
{
    switch (b->view.phase) {
    case APP_BOOTH_LIVE: return tap_live(b, x, y, filters_owned, lenses_owned, film_owned, fainted, code);
    case APP_BOOTH_GALLERY: return tap_gallery(b, x, y);
    case APP_BOOTH_REVIEW:
        set_phase(b, APP_BOOTH_LIVE);
        return bump(b);
    default: return 0;   /* Countdown, flash and recording ignore taps. */
    }
}

uint32_t booth_update(booth_t *b)
{
    app_booth_snapshot_t *v = &b->view;
    ++v->phase_ticks;
    switch (v->phase) {
    case APP_BOOTH_COUNTDOWN:
        if (v->phase_ticks < COUNT_TICKS) {
            return 0;
        }
        v->phase_ticks = 0;
        if (v->count > 1) {
            --v->count;
            return bump(b);
        }
        if (v->gif) {
            set_phase(b, APP_BOOTH_RECORD);
            request(b, APP_BOOTH_REQ_GIF_FRAME, 0, 0);
            v->frame = 1;
        } else {
            set_phase(b, APP_BOOTH_FLASH);
            request(b, APP_BOOTH_REQ_PHOTO, 0, 0);
        }
        return bump(b);
    case APP_BOOTH_FLASH:
        if (v->phase_ticks >= FLASH_TICKS) {
            set_phase(b, APP_BOOTH_REVIEW);
            return bump(b);
        }
        return 0;
    case APP_BOOTH_RECORD:
        if (v->phase_ticks < GIF_FRAME_TICKS) {
            return 0;
        }
        v->phase_ticks = 0;
        if (v->frame >= APP_PHOTO_GIF_FRAMES) {
            set_phase(b, APP_BOOTH_REVIEW);   /* The last frame's save reports a result. */
            return bump(b);
        }
        request(b, APP_BOOTH_REQ_GIF_FRAME, v->frame, 0);
        ++v->frame;
        return 0;
    case APP_BOOTH_REVIEW:
        if (v->phase_ticks >= REVIEW_TICKS && v->result != APP_BOOTH_RESULT_NONE) {
            set_phase(b, APP_BOOTH_LIVE);
            return bump(b);
        }
        return 0;
    default:
        return 0;
    }
}

void booth_set_status(booth_t *b, bool camera, uint8_t photos, uint8_t gifs)
{
    b->view.camera = camera;
    b->view.photos = photos > APP_BOOTH_PHOTOS ? APP_BOOTH_PHOTOS : photos;
    b->view.gifs = gifs > APP_BOOTH_GIFS ? APP_BOOTH_GIFS : gifs;
    const uint8_t count = items(b);
    if (b->view.index >= count) {
        b->view.index = count ? (uint8_t)(count - 1) : 0;
    }
}

uint32_t booth_result(booth_t *b, app_booth_result_t result)
{
    b->view.result = result;
    return bump(b);
}
