// SPDX-License-Identifier: Apache-2.0
/* Photo booth media: feed -> filter -> capture -> gallery, on the game loop. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "private/booth_media.h"
#include "../app_photo_store/include/app_photo_store.h"

#if defined(RAYLIB_LITE_GAME_NATIVE)
#include "app_cam_stream.h"
#include "app_camera_hw.h"
#include "esp_log.h"
static const char *TAG = "booth";
#endif

_Static_assert((int)APP_PHOTO_STORE_PHOTOS == (int)APP_BOOTH_PHOTOS && (int)APP_PHOTO_STORE_GIFS == (int)APP_BOOTH_GIFS,
               "model and store slot counts must match");

enum {
    FAKE_FRAME_TICKS = 2,      /* Host: 15 synthetic frames per second. */
    NO_FILTER_YET = 0xFF,
};

void booth_media_init(booth_media_t *m)
{
    memset(m, 0, sizeof(*m));
    m->shown_index = -1;
    m->live_filter = NO_FILTER_YET;
    m->host_present = true;   /* Host: the right key unplugs/replugs the fake camera. */
    m->store_ready = app_photo_store_init() == APP_PHOTO_STORE_OK;
}

#if !defined(RAYLIB_LITE_GAME_NATIVE)
/* Host: a face drifting over sky and grass, a red ball, sensor-like noise, so
 * every booth screen, look and both films can be exercised without a camera. */
static void fake_frame(booth_media_t *m)
{
    enum { S = APP_PHOTO_MAX_SIDE, N = S * S };
    const float t = (float)m->fake_t * 0.06f;
    const int cx = S / 2 + (int)(28.0f * sinf(t)), cy = S / 2 - 4 + (int)(8.0f * cosf(t * 1.3f));
    uint32_t rng = m->fake_t * 2654435761U + 1U;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const int dx = x - cx, dy = y - cy, bx = x - 104, by = y - 100;
            int r = y < S / 2 ? 60 + y : 50, g = y < S / 2 ? 110 + y : 150 + (y - S / 2) / 2,
                b = y < S / 2 ? 230 : 60;   /* Sky, then grass. */
            if (dx * dx + dy * dy < 34 * 34) {
                r = 230, g = 180, b = 140;   /* Face. */
            }
            const int ex = abs(dx) - 12, ey = dy + 8;
            if (ex * ex + ey * ey < 28 || ((dy == 14 || dy == 15) && abs(dx) < 14)) {
                r = g = b = 36;   /* Eyes and a smile. */
            }
            if (bx * bx + by * by < 12 * 12) {
                r = 220, g = 40, b = 40;   /* Red ball. */
            }
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            const int noise = (int)(rng >> 28) - 8;
            const int Y = (299 * r + 587 * g + 114 * b) / 1000 + noise;
            const int U = 128 + (b - Y) * 564 / 1000, V = 128 + (r - Y) * 713 / 1000;
            m->yuv[y * S + x] = (uint8_t)(Y < 0 ? 0 : (Y > 255 ? 255 : Y));
            m->yuv[N + y * S + x] = (uint8_t)(U < 0 ? 0 : (U > 255 ? 255 : U));
            m->yuv[2 * N + y * S + x] = (uint8_t)(V < 0 ? 0 : (V > 255 ? 255 : V));
        }
    }
    ++m->luma_seq;
}
#endif

/* Feed scaled to a lens (all three planes for color, Y alone for B&W), then
 * through the look: palette indices, one byte per pixel. */
static void filter_frame(booth_media_t *m, int res, int filter, bool color, uint8_t *out)
{
    enum { N = APP_PHOTO_MAX_PIXELS };
    const int side = app_photo_side(res), n = side * side;
    for (int plane = 0; plane < (color ? 3 : 1); ++plane) {
        app_photo_downscale(m->yuv + plane * N, APP_PHOTO_MAX_SIDE, m->work_yuv + plane * n, side);
    }
    if (color) {
        app_photo_filter_color(m->work_yuv, side, (app_photo_filter_t)filter, out);
    } else {
        app_photo_filter(m->work_yuv, side, (app_photo_filter_t)filter, out);
    }
}

static bool booth_open(const app_game_snapshot_t *s)
{
    return s->screen == APP_SCREEN_BOOTH && (s->owned & APP_OWN_GAME_BIT(APP_GAME_PHOTO))
           && s->overlay == APP_OVERLAY_NONE && s->power != APP_POWER_DOZE;
}

/* Probe for the camera only where it changes what is shown: the arcade (the
 * booth page appears to its right), the booth page and the store. */
static bool presence_wanted(const app_game_snapshot_t *s)
{
    return s->power != APP_POWER_DOZE
           && (s->screen == APP_SCREEN_ARCADE || s->screen == APP_SCREEN_BOOTH || s->overlay == APP_OVERLAY_STORE);
}

static void run_feed(booth_media_t *m, const app_game_snapshot_t *s)
{
    const bool want = booth_open(s) && (s->booth.phase != APP_BOOTH_GALLERY || s->booth.on_air);
    const bool stream = want || m->face_feed;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    app_cam_stream_set(s->booth.on_air, s->booth.cam_code);
    app_camera_hw_set_mode(stream ? APP_CAMERA_MODE_STREAM
                           : (presence_wanted(s) ? APP_CAMERA_MODE_PROBE : APP_CAMERA_MODE_OFF));
    m->present = app_camera_hw_present();
    static const booth_camera_t map[] = {
        BOOTH_CAMERA_OFF, BOOTH_CAMERA_STARTING, BOOTH_CAMERA_LIVE, BOOTH_CAMERA_ABSENT, BOOTH_CAMERA_ERROR,
    };
    const app_camera_state_t state = app_camera_hw_state();
    m->camera = (unsigned)state < sizeof(map) / sizeof(map[0]) ? map[state] : BOOTH_CAMERA_ERROR;
    if (want) {
        app_camera_hw_frame(m->yuv, &m->luma_seq);
    }
#else
    (void)presence_wanted;
    m->present = m->host_present;
    m->camera = !m->present ? BOOTH_CAMERA_ABSENT : (want || m->luma_seq ? BOOTH_CAMERA_LIVE : BOOTH_CAMERA_OFF);
    if (want && m->present && m->fake_t++ % FAKE_FRAME_TICKS == 0U) {
        fake_frame(m);
    }
#endif
    const bool color = s->booth.color && app_photo_filter_has_color(s->booth.filter);
    if (m->luma_seq && (m->live_seq != m->luma_seq || m->live_filter != s->booth.filter
                        || m->live_res != s->booth.res || m->live_color != color)) {
        filter_frame(m, s->booth.res, s->booth.filter, color, m->live);
        m->live_seq = m->luma_seq;
        m->live_filter = s->booth.filter;
        m->live_res = s->booth.res;
        m->live_color = color;
    }
}

/* One packed frame of the current feed at a lens, look and film. Captures are
 * made fresh (not from `live`) so a GIF can record at its capped lens. */
static void make_frame(booth_media_t *m, int res, int filter, bool color, uint8_t *packed)
{
    const int side = app_photo_side(res);
    filter_frame(m, res, filter, color, m->work_shades);
    if (color) {
        app_photo_pack4(m->work_shades, side * side, packed);
    } else {
        app_photo_pack(m->work_shades, side * side, packed);
    }
}

static app_booth_result_t save_capture(booth_media_t *m)
{
    const app_photo_store_err_t err = app_photo_store_save(&m->capture);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    ESP_LOGI(TAG, "saved %s seq %lu: %d", m->capture.header.frames > 1 ? "gif" : "photo",
             (unsigned long)m->capture.header.seq, (int)err);
#endif
    switch (err) {
    case APP_PHOTO_STORE_OK: return APP_BOOTH_RESULT_SAVED;
    case APP_PHOTO_STORE_ERR_FULL: return APP_BOOTH_RESULT_FULL;
    default: return APP_BOOTH_RESULT_FAILED;
    }
}

static void begin_capture(booth_media_t *m, const app_booth_request_t *req, uint8_t frames, app_photo_rgb_t base)
{
    memset(&m->capture.header, 0, sizeof(m->capture.header));
    m->capture.header.filter = req->filter;
    m->capture.header.frames = frames;
    m->capture.header.base = base;
    const uint8_t cap = req->color ? APP_PHOTO_GIF_MAX_RES_COLOR : APP_PHOTO_GIF_MAX_RES;
    const uint8_t lens = frames > 1 && req->res > cap ? cap : req->res;
    m->capture.header.res = (uint8_t)(lens | (req->color ? APP_PHOTO_COLOR_FLAG : 0));
}

static void serve_request(booth_media_t *m, app_game_handle_t game, app_photo_rgb_t base)
{
    app_booth_request_t req;
    if (!app_game_take_booth_request(game, &req)) {
        return;
    }
    const bool have_frame = m->luma_seq != 0;
    switch (req.kind) {
    case APP_BOOTH_REQ_PHOTO:
        if (!have_frame) {
            app_game_booth_result(game, APP_BOOTH_RESULT_FAILED);
            return;
        }
        begin_capture(m, &req, 1, base);
        make_frame(m, app_photo_lens(&m->capture.header), req.filter, req.color, app_photo_frame(&m->capture, 0));
        app_game_booth_result(game, save_capture(m));
        return;
    case APP_BOOTH_REQ_GIF_FRAME:
        if (req.frame == 0) {
            begin_capture(m, &req, APP_PHOTO_GIF_FRAMES, base);
        }
        if (!have_frame || req.frame >= APP_PHOTO_GIF_FRAMES) {
            app_game_booth_result(game, APP_BOOTH_RESULT_FAILED);
            return;
        }
        make_frame(m, app_photo_lens(&m->capture.header), req.filter, req.color,
                   app_photo_frame(&m->capture, req.frame));
        if (req.frame == APP_PHOTO_GIF_FRAMES - 1) {
            app_game_booth_result(game, save_capture(m));
        }
        return;
    case APP_BOOTH_REQ_DELETE:
        app_game_booth_result(game, app_photo_store_delete(req.index) == APP_PHOTO_STORE_OK
                              ? APP_BOOTH_RESULT_DELETED : APP_BOOTH_RESULT_FAILED);
        m->shown_index = -1;
        return;
    default:
        return;
    }
}

static void cache_gallery(booth_media_t *m, const app_game_snapshot_t *s)
{
    if (s->booth.phase != APP_BOOTH_GALLERY) {
        return;
    }
    const uint32_t revision = app_photo_store_revision();
    if (m->shown_index == s->booth.index && m->shown_revision == revision) {
        return;
    }
    m->shown_revision = revision;
    m->shown_index = s->booth.index;
    m->shown_valid = app_photo_store_load(s->booth.index, &m->shown) == APP_PHOTO_STORE_OK;
}

void booth_media_update(booth_media_t *m, app_game_handle_t game, app_photo_rgb_t base)
{
    app_game_snapshot_t s = app_game_read(game);
    run_feed(m, &s);
    app_game_set_camera(game, m->present);
    serve_request(m, game, base);
    uint8_t photos = 0, gifs = 0;
    app_photo_store_counts(&photos, &gifs);
    app_game_set_booth(game, m->camera == BOOTH_CAMERA_LIVE && m->live_seq != 0, photos, gifs);
    cache_gallery(m, &s);
}
