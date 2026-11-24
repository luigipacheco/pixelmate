// SPDX-License-Identifier: Apache-2.0
/* Photo booth media: feed -> filter -> capture -> gallery, on the game loop. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "private/booth_media.h"
#include "../app_photo_store/include/app_photo_store.h"

#if defined(RAYLIB_LITE_GAME_NATIVE)
#include <stdatomic.h>
#include "app_cam_stream.h"
#include "app_camera_hw.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "booth";
#endif

enum {
    FAKE_FRAME_TICKS = 2,      /* Host: 15 synthetic frames per second. */
    NO_FILTER_YET = 0xFF,
};

static app_photo_t *record_alloc(size_t size)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, size);
#else
    return calloc(1, size);
#endif
}

void booth_media_init(booth_media_t *m)
{
    memset(m, 0, sizeof(*m));
    m->shown_index = -1;
    m->live_filter = NO_FILTER_YET;
    m->host_present = true;   /* Host: the right key unplugs/replugs the fake camera. */
    m->store_ready = app_photo_store_init() == APP_PHOTO_STORE_OK;
    /* Records as big as the store keeps: 64 KB (an 8-frame HI color GIF) on
     * files and RAM, 8 KB on NVS. Allocated once, touched only by captures,
     * saves and gallery loads, never per tick. */
    app_photo_store_usage_t use;
    app_photo_store_usage(&use);
    m->big = m->store_ready && use.big_records;
    m->record_cap = use.max_record ? use.max_record : APP_PHOTO_SMALL_RECORD_MAX;
    m->capture = record_alloc(m->record_cap);
    m->shown = record_alloc(m->record_cap);
    if (!m->capture || !m->shown) {
        free(m->capture);
        free(m->shown);
        m->capture = m->shown = NULL;
#if defined(RAYLIB_LITE_GAME_NATIVE)
        ESP_LOGE(TAG, "no memory for two %u B photo records", (unsigned)m->record_cap);
#endif
    }
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
           && (s->screen == APP_SCREEN_ARCADE || s->screen == APP_SCREEN_TOOLS || s->screen == APP_SCREEN_BOOTH
               || s->overlay == APP_OVERLAY_STORE);
}

static void run_feed(booth_media_t *m, const app_game_snapshot_t *s)
{
    const bool want = booth_open(s) && (s->booth.phase != APP_BOOTH_GALLERY || s->booth.on_air);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    app_cam_stream_set(s->booth.on_air, s->booth.cam_code);
    app_camera_hw_set_mode(want ? APP_CAMERA_MODE_STREAM
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

/* One packed frame of the capture's lens, look and film. The viewfinder's
 * picture is reused when it matches (run_feed has just made it from the
 * newest feed frame), so a GIF at the viewfinder's lens costs only the
 * packing; otherwise (a GIF capped to a smaller lens) the frame is made fresh. */
static void make_frame(booth_media_t *m, uint8_t *packed)
{
    const app_photo_header_t *h = &m->capture->header;
    const int res = app_photo_lens(h), side = app_photo_side(res);
    const bool color = app_photo_is_color(h);
    const uint8_t *indices = m->live;
    if (m->live_seq != m->luma_seq || m->live_res != res || m->live_filter != h->filter
            || m->live_color != color) {
        filter_frame(m, res, h->filter, color, m->work_shades);
        indices = m->work_shades;
    }
    if (color) {
        app_photo_pack4(indices, side * side, packed);
    } else {
        app_photo_pack(indices, side * side, packed);
    }
}

static app_booth_result_t result_of(app_photo_store_err_t err)
{
    switch (err) {
    case APP_PHOTO_STORE_OK: return APP_BOOTH_RESULT_SAVED;
    case APP_PHOTO_STORE_ERR_FULL: return APP_BOOTH_RESULT_FULL;
    default: return APP_BOOTH_RESULT_FAILED;
    }
}

#if defined(RAYLIB_LITE_GAME_NATIVE)
/* Device: a small task writes captures, so a HI GIF (32 or 64 KB of flash
 * writes) never stalls the game loop. While it runs the game loop leaves the
 * store alone (the writer holds the store's lock) and only reads `capture`
 * to show it. */
enum { SAVER_STACK = 6144, SAVER_PRIORITY = 2 };
static TaskHandle_t s_saver;
static app_photo_t *_Atomic s_save_record;
static _Atomic int s_save_err;
static _Atomic bool s_save_done;

static void saver(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        app_photo_t *record = atomic_load(&s_save_record);
        const int64_t start = esp_timer_get_time();
        const app_photo_store_err_t err = app_photo_store_save(record);
        ESP_LOGI(TAG, "saved %s seq %lu, %u B in %lu ms: %d", record->header.frames > 1 ? "gif" : "photo",
                 (unsigned long)record->header.seq, (unsigned)app_photo_record_size(&record->header),
                 (unsigned long)((esp_timer_get_time() - start) / 1000), (int)err);
        atomic_store(&s_save_err, (int)err);
        atomic_store(&s_save_done, true);
    }
}

/* Report a finished background save. */
static void poll_save(booth_media_t *m, app_game_handle_t game)
{
    if (m->saving && atomic_load(&s_save_done)) {
        m->saving = false;
        app_game_booth_result(game, result_of((app_photo_store_err_t)atomic_load(&s_save_err)));
    }
}

/* Before anything that writes `capture` or uses the store; waits only when a
 * request comes during a save (it takes about a second at most). */
static void wait_save(booth_media_t *m, app_game_handle_t game)
{
    while (m->saving) {
        poll_save(m, game);
        if (m->saving) {
            vTaskDelay(1);
        }
    }
}
#else
static void poll_save(booth_media_t *m, app_game_handle_t game)
{
    (void)m;
    (void)game;
}

static void wait_save(booth_media_t *m, app_game_handle_t game)
{
    (void)m;
    (void)game;
}
#endif

/* Save the capture; the result reaches the model now (Host) or when the save
 * task is done (device). */
static void save_capture(booth_media_t *m, app_game_handle_t game)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (!s_saver && xTaskCreate(saver, "photo_save", SAVER_STACK, NULL, SAVER_PRIORITY, &s_saver) != pdPASS) {
        s_saver = NULL;
        ESP_LOGW(TAG, "no save task; saving on the game loop");
    }
    if (s_saver) {
        atomic_store(&s_save_done, false);
        atomic_store(&s_save_record, m->capture);
        m->saving = true;
        xTaskNotifyGive(s_saver);
        return;
    }
#endif
    app_game_booth_result(game, result_of(app_photo_store_save(m->capture)));
}

/* The header a capture gets: look, frame count, lens and film. GIFs keep to
 * the store's limits: every lens on big stores (ESP-Mosaico), else B&W up to
 * MID and color LO. */
static app_photo_header_t capture_header(const booth_media_t *m, uint8_t filter, uint8_t res, bool color,
                                         uint8_t frames, app_photo_rgb_t base)
{
    const int lens = frames > 1 ? app_photo_gif_lens(res, color, m->big) : res;
    return (app_photo_header_t) {
        .filter = filter, .frames = frames, .base = base,
        .res = (uint8_t)(lens | (color ? APP_PHOTO_COLOR_FLAG : 0)),
    };
}

/* Start a capture; false when it cannot be held (no record memory). */
static bool begin_capture(booth_media_t *m, const app_booth_request_t *req, uint8_t frames, app_photo_rgb_t base)
{
    if (!m->capture) {
        return false;
    }
    m->capture->header = capture_header(m, req->filter, req->res, req->color, frames, base);
    return app_photo_record_size(&m->capture->header) <= m->record_cap;
}

static void serve_request(booth_media_t *m, app_game_handle_t game, app_photo_rgb_t base)
{
    app_booth_request_t req;
    if (!app_game_take_booth_request(game, &req)) {
        return;
    }
    wait_save(m, game);   /* Normally long done: a new capture starts with a countdown. */
    const bool have_frame = m->luma_seq != 0;
    switch (req.kind) {
    case APP_BOOTH_REQ_PHOTO:
        if (!have_frame || !begin_capture(m, &req, 1, base)) {
            app_game_booth_result(game, APP_BOOTH_RESULT_FAILED);
            return;
        }
        make_frame(m, app_photo_frame(m->capture, 0));
        save_capture(m, game);
        return;
    case APP_BOOTH_REQ_GIF_FRAME: {
        const bool ok = req.frame != 0 || begin_capture(m, &req, APP_PHOTO_GIF_FRAMES, base);
        if (!have_frame || !ok || !m->capture || m->capture->header.frames != APP_PHOTO_GIF_FRAMES
                || req.frame >= APP_PHOTO_GIF_FRAMES) {
            app_game_booth_result(game, APP_BOOTH_RESULT_FAILED);
            return;
        }
        make_frame(m, app_photo_frame(m->capture, req.frame));
        if (req.frame == APP_PHOTO_GIF_FRAMES - 1) {
            save_capture(m, game);
        }
        return;
    }
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
    if (s->booth.phase != APP_BOOTH_GALLERY || !m->shown) {
        return;
    }
    const uint32_t revision = app_photo_store_revision();
    if (m->shown_index == s->booth.index && m->shown_revision == revision) {
        return;
    }
    m->shown_revision = revision;
    m->shown_index = s->booth.index;
    m->shown_valid = app_photo_store_load(s->booth.index, m->shown, m->record_cap) == APP_PHOTO_STORE_OK;
}

/* Ask the store about the roll again when it changes or the booth does
 * (booth.seq moves with every look, lens, film or phase change): counts,
 * limits, free space and whether a photo and a GIF at the selected lens and
 * film fit. Without a store every capture is allowed, and its save then
 * reports the failure. */
static void refresh_roll(booth_media_t *m, const app_game_snapshot_t *s)
{
    const app_booth_snapshot_t *b = &s->booth;
    const uint32_t revision = app_photo_store_revision();
    if (m->roll_valid && m->roll_revision == revision && m->roll_seq == b->seq) {
        return;
    }
    m->roll_valid = true;
    m->roll_revision = revision;
    m->roll_seq = b->seq;
    const bool color = b->color && app_photo_filter_has_color(b->filter);
    app_photo_store_usage_t use;
    app_photo_store_usage(&use);
    const uint32_t free_kb = use.free_bytes / 1024U;
    const app_photo_rgb_t base = {0, 0, 0};
    const app_photo_header_t photo = capture_header(m, b->filter, b->res, color, 1, base);
    const app_photo_header_t gif = capture_header(m, b->filter, b->res, color, APP_PHOTO_GIF_FRAMES, base);
    m->roll = (app_booth_roll_t) {
        .photos = use.photos, .gifs = use.gifs,
        .max_photos = use.max_photos, .max_gifs = use.max_gifs,
        .free_kb = (uint16_t)(free_kb > UINT16_MAX ? UINT16_MAX : free_kb),
        .photo_room = !m->store_ready || app_photo_store_fits(&photo),
        .gif_room = !m->store_ready || app_photo_store_fits(&gif),
    };
}

void booth_media_update(booth_media_t *m, app_game_handle_t game, app_photo_rgb_t base)
{
    app_game_snapshot_t s = app_game_read(game);
    run_feed(m, &s);
    app_game_set_camera(game, m->present);
    poll_save(m, game);
    serve_request(m, game, base);
    if (!m->saving) {   /* The save task holds the store's lock meanwhile. */
        refresh_roll(m, &s);
    }
    app_game_set_booth(game, m->camera == BOOTH_CAMERA_LIVE && m->live_seq != 0, &m->roll);
    if (!m->saving) {
        cache_gallery(m, &s);
    }
}
