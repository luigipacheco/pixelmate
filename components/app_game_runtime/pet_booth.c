// SPDX-License-Identifier: Apache-2.0
/* Photo booth screens: viewfinder with filter picker, 3-2-1 countdown, flash,
 * GIF recording, review and gallery. Also the cabinet's attract screen and the
 * store's filter swatches. Pixels come from booth_media (runtime-owned). */
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color PAPER = {240, 240, 232, 255};
static const Color WARN = {255, 48, 48, 255};
static const Color SHADOW = {12, 12, 12, 255};

enum {
    LABEL_Y = 84,             /* Filter name / status line, between EXIT and the eyes. */
    BEZEL = 10,               /* Viewfinder frame around the picture. */
    BRACKET = 28,             /* Corner bracket arm length. */
    GIF_SHOW_TICKS = 4,       /* Playback speed of GIFs in review and gallery. */
    FLASH_FULL_TICKS = 4,     /* Whole screen white, then only the picture. */
    REC_BLINK_TICKS = 8,
};

static const char *const ARROW_LEFT[] = {
    "    ##", "   ###", "  ####", " #####", "######", "######", " #####", "  ####", "   ###", "    ##", NULL,
};
static const char *const ARROW_RIGHT[] = {
    "##    ", "###   ", "####  ", "##### ", "######", "######", "##### ", "####  ", "###   ", "##    ", NULL,
};

static Color to_color(app_photo_rgb_t c, float level)
{
    return px_scale((Color) {c.r, c.g, c.b, 255}, level);
}

static void palette_colors(int filter, Color base, float level, Color out[APP_PHOTO_SHADES])
{
    app_photo_rgb_t pal[APP_PHOTO_SHADES];
    app_photo_palette((app_photo_filter_t)filter, (app_photo_rgb_t) {base.r, base.g, base.b}, pal);
    for (int i = 0; i < APP_PHOTO_SHADES; ++i) {
        out[i] = to_color(pal[i], level);
    }
}

/* Pixel i of `data`: one byte each (bits 0) or packed at 2 or 4 bits. */
static uint8_t index_at(const uint8_t *data, int bits, int i)
{
    switch (bits) {
    case 2: return (uint8_t)((data[i >> 2] >> (6 - 2 * (i & 3))) & 3U);
    case 4: return (uint8_t)((data[i >> 1] >> ((i & 1) ? 0 : 4)) & 15U);
    default: return data[i];
    }
}

/* Color film palette at the screen's brightness. */
static void film_colors(float level, Color out[APP_PHOTO_COLORS])
{
    for (int i = 0; i < APP_PHOTO_COLORS; ++i) {
        out[i] = to_color(app_photo_color_palette[i], level);
    }
}

/* One rectangle per horizontal run of equal indices keeps this to a few
 * hundred draw calls per frame instead of thousands. */
static void draw_photo(const uint8_t *data, int bits, int w, int h, int x0, int y0, int px,
                       const Color pal[APP_PHOTO_COLORS])
{
    for (int y = 0; y < h; ++y) {
        int start = 0;
        uint8_t cur = index_at(data, bits, y * w);
        for (int x = 1; x <= w; ++x) {
            const uint8_t next = x < w ? index_at(data, bits, y * w + x) : 0xFF;
            if (next != cur) {
                DrawRectangle(x0 + start * px, y0 + y * px, (x - start) * px, px, pal[cur & 15U]);
                start = x;
                cur = next;
            }
        }
    }
}

static void draw_stored(const draw_ctx_t *d, const app_photo_t *photo, int x, int y, uint32_t t)
{
    const int side = app_photo_side(app_photo_lens(&photo->header)), px = app_booth_view_rect().w / side;
    const bool color = app_photo_is_color(&photo->header);
    Color pal[APP_PHOTO_COLORS];
    if (color) {
        film_colors(d->level, pal);
    } else {
        const Color base = {photo->header.base.r, photo->header.base.g, photo->header.base.b, 255};
        palette_colors(photo->header.filter, base, d->level, pal);
    }
    const int frames = photo->header.frames ? photo->header.frames : 1;
    const int frame = (int)((t / GIF_SHOW_TICKS) % (uint32_t)frames);
    draw_photo(app_photo_frame_const(photo, frame), color ? 4 : 2, side, side, x, y, px, pal);
}

static void draw_frame(app_rect_t r, int ox, Color c)
{
    const int x = r.x + ox - BEZEL, y = r.y - BEZEL, w = r.w + 2 * BEZEL, h = r.h + 2 * BEZEL;
    DrawRectangle(x, y, w, h, px_scale(c, 0.12f));
    /* Corner brackets, like a camera's focus frame. */
    for (int i = 0; i < 4; ++i) {
        const int cx = i & 1 ? x + w - 4 : x, cy = i & 2 ? y + h - 4 : y;
        DrawRectangle(i & 1 ? cx - BRACKET + 4 : cx, cy, BRACKET, 4, c);
        DrawRectangle(cx, i & 2 ? cy - BRACKET + 4 : cy, 4, BRACKET, c);
    }
}

static void center_text(const draw_ctx_t *d, const char *text, int ox, int y, int scale, Color c)
{
    px_text_center(text, 240 + ox, y, scale, c, d->gap);
}

/* Message inside an empty viewfinder (no camera, empty gallery...). */
static void view_message(const draw_ctx_t *d, int ox, const char *a, const char *b, Color c)
{
    const app_rect_t r = app_booth_view_rect();
    const int mid = r.y + r.h / 2;
    center_text(d, a, ox, b ? mid - 30 : mid - 10, 3, c);
    if (b) {
        px_text_center(b, 240 + ox, mid + 10, 2, d->dim, false);
    }
}

static void draw_arrows(const draw_ctx_t *d, int ox, Color c)
{
    const app_rect_t l = app_booth_prev_rect(), r = app_booth_next_rect();
    const int iw = px_icon_width(ARROW_LEFT) * 5, ih = px_icon_height(ARROW_LEFT) * 5;
    px_icon(ARROW_LEFT, l.x + ox + (l.w - iw) / 2, l.y + (l.h - ih) / 2, 5, c, c, d->gap);
    px_icon(ARROW_RIGHT, r.x + ox + (r.w - iw) / 2, r.y + (r.h - ih) / 2, 5, c, c, d->gap);
}

static int count_bits(unsigned bits)
{
    int n = 0;
    for (; bits; bits &= bits - 1U) {
        ++n;
    }
    return n;
}

static void draw_live_picture(const draw_ctx_t *d, int ox)
{
    const booth_media_t *m = d->view->media;
    const app_rect_t r = app_booth_view_rect();
    const app_game_snapshot_t *s = d->snap;
    if (!m || m->camera != BOOTH_CAMERA_LIVE || !m->live_seq) {
        switch (m ? m->camera : BOOTH_CAMERA_ERROR) {
        case BOOTH_CAMERA_ABSENT: view_message(d, ox, "NO CAMERA", "PLUG IT IN THE LEFT SLOT", WARN); break;
        case BOOTH_CAMERA_ERROR: view_message(d, ox, "CAMERA ERROR", "TRY AGAIN LATER", WARN); break;
        default:
            view_message(d, ox, (d->view->t / 10U) % 2U ? "WAKING UP" : "WAKING UP...", NULL, d->color);
            break;
        }
        return;
    }
    Color pal[APP_PHOTO_COLORS];
    if (m->live_color) {
        film_colors(d->level, pal);
    } else {
        palette_colors(s->booth.filter, px_color(s->color_index), d->level, pal);
    }
    const int side = app_photo_side(m->live_res);
    draw_photo(m->live, 0, side, side, r.x + ox, r.y, r.w / side, pal);
}

static void draw_live(const draw_ctx_t *d, int ox)
{
    const app_game_snapshot_t *s = d->snap;
    const app_booth_snapshot_t *b = &s->booth;
    char text[16];
    draw_live_picture(d, ox);
    if (b->on_air) {
        /* Red ON AIR mark with the viewer code in place of the look's name. */
        char air[24];
        snprintf(air, sizeof(air), "ON AIR %04u", (unsigned)b->cam_code);
        const int w = px_text_width(air, 3);
        if ((d->view->t / REC_BLINK_TICKS) % 2U) {
            px_box(240 + ox - w / 2 - 26, LABEL_Y, 18, 21, 0, WARN);
        }
        px_text(air, 240 + ox - w / 2, LABEL_Y, 3, WARN, d->gap);
    } else {
        center_text(d, app_photo_filter_name(b->filter), ox, LABEL_Y, 3, d->color);
    }
    /* Webcam chip: dim until WEBCAM is owned; red STOP while streaming. */
    const bool cam = (s->lenses_owned & APP_OWN_WEBCAM_BIT) != 0;
    screens_button(d, app_booth_air_rect(), ox, 0, b->on_air ? "STOP" : "ON AIR", 2,
                   b->on_air ? WARN : (cam ? d->color : d->dim), b->on_air);
    const bool many = count_bits(s->filters_owned | APP_OWN_FILTER_BIT(APP_FILTER_NONE)) > 1;
    draw_arrows(d, ox, many ? d->color : d->dim);
    /* Lens chip: tap to switch resolution. */
    const bool lenses = count_bits(s->lenses_owned | APP_OWN_LENS_BIT(APP_PHOTO_RES_LOW)) > 1;
    screens_button(d, app_booth_lens_rect(), ox, 0, app_photo_res_name(b->res), 2,
                   lenses ? d->color : d->dim, false);
    /* Film chip: B/W or COLOR. Dim until COLOR FILM is owned, and for GREEN
     * and THERMAL, which always shoot B/W. */
    const bool film = (s->lenses_owned & APP_OWN_FILM_BIT) && app_photo_filter_has_color(b->filter);
    const bool color = b->color && app_photo_filter_has_color(b->filter);
    screens_button(d, app_booth_film_rect(), ox, 0, color ? "COLOR" : "B/W", 2,
                   film ? (color ? d->accent : d->color) : d->dim, color);
    const bool ready = b->camera && !s->fainted;
    screens_button(d, app_booth_button_rect(0), ox, 0, "GIF", 4,
                   ready && b->gifs < APP_BOOTH_GIFS ? d->accent : d->dim, false);
    screens_button(d, app_booth_button_rect(1), ox, 0, "SNAP", 4,
                   ready && b->photos < APP_BOOTH_PHOTOS ? d->color : d->dim, ready);
    snprintf(text, sizeof(text), "ROLL %u", (unsigned)(b->photos + b->gifs));
    screens_button(d, app_booth_button_rect(2), ox, 0, text, 3, d->color, false);
}

static void draw_countdown(const draw_ctx_t *d, int ox)
{
    const app_booth_snapshot_t *b = &d->snap->booth;
    draw_live_picture(d, ox);
    center_text(d, b->gif ? "GIF IN" : "SMILE!", ox, LABEL_Y, 3, d->accent);
    /* The digit lands big and settles, once per beat. */
    const int scale = 18 - (b->phase_ticks < 6 ? 0 : (b->phase_ticks - 6) / 3);
    char digit[2] = {(char)('0' + b->count), 0};
    const app_rect_t r = app_booth_view_rect();
    const int size = scale < 10 ? 10 : scale;
    px_text_center(digit, r.x + r.w / 2 + ox + 3, r.y + r.h / 2 - 7 * size / 2 + 3, size, SHADOW, false);
    px_text_center(digit, r.x + r.w / 2 + ox, r.y + r.h / 2 - 7 * size / 2, size, PAPER, false);
}

static void draw_record(const draw_ctx_t *d, int ox)
{
    const app_booth_snapshot_t *b = &d->snap->booth;
    draw_live_picture(d, ox);
    const bool blink = (d->view->t / REC_BLINK_TICKS) % 2U;
    const int w = px_text_width("REC", 3);
    if (blink) {
        px_box(240 + ox - w / 2 - 26, LABEL_Y, 18, 21, 0, WARN);
    }
    px_text("REC", 240 + ox - w / 2, LABEL_Y, 3, WARN, d->gap);
    /* One segment per recorded frame along the bottom of the frame. */
    const app_rect_t r = app_booth_view_rect();
    const int seg = r.w / APP_PHOTO_GIF_FRAMES;
    for (int i = 0; i < APP_PHOTO_GIF_FRAMES; ++i) {
        DrawRectangle(r.x + ox + i * seg + 2, r.y + r.h + BEZEL + 4, seg - 4, 6, i < b->frame ? WARN : d->dim);
    }
}

static void draw_review(const draw_ctx_t *d, int ox)
{
    const booth_media_t *m = d->view->media;
    const app_booth_snapshot_t *b = &d->snap->booth;
    const app_rect_t r = app_booth_view_rect();
    const char *label = "SAVING...";
    Color c = d->color;
    switch (b->result) {
    case APP_BOOTH_RESULT_SAVED: label = b->gif ? "GIF SAVED!" : "PHOTO SAVED!"; c = d->accent; break;
    case APP_BOOTH_RESULT_FULL: label = "ROLL IS FULL"; c = WARN; break;
    case APP_BOOTH_RESULT_FAILED: label = "OOPS, NOT SAVED"; c = WARN; break;
    default: break;
    }
    center_text(d, label, ox, LABEL_Y, 3, c);
    if (b->result == APP_BOOTH_RESULT_FULL) {
        view_message(d, ox, "DELETE SOME", "IN THE ROLL", d->color);
    } else if (m && m->capture.header.frames) {
        /* A white print border around the fresh capture. */
        DrawRectangle(r.x + ox - BEZEL, r.y - BEZEL, r.w + 2 * BEZEL, r.h + 2 * BEZEL, px_scale(PAPER, d->level));
        draw_stored(d, &m->capture, r.x + ox, r.y, d->view->t);
    }
    px_text_center("TAP TO GO ON", 240 + ox, 398 + 18, 2, d->dim, false);
}

static void draw_gallery(const draw_ctx_t *d, int ox)
{
    const booth_media_t *m = d->view->media;
    const app_booth_snapshot_t *b = &d->snap->booth;
    const app_rect_t r = app_booth_view_rect();
    const int count = b->photos + b->gifs;
    char text[24];
    if (!count) {
        center_text(d, "ROLL", ox, LABEL_Y, 3, d->color);
        view_message(d, ox, "NO PHOTOS YET", "SNAP ONE!", d->color);
    } else {
        const bool gif = m && m->shown_valid && m->shown.header.frames > 1;
        snprintf(text, sizeof(text), "%s %d/%d", gif ? "GIF" : "PIC", b->index + 1, count);
        center_text(d, text, ox, LABEL_Y, 3, d->color);
        if (m && m->shown_valid) {
            draw_stored(d, &m->shown, r.x + ox, r.y, d->view->t);
        }
        draw_arrows(d, ox, count > 1 ? d->color : d->dim);
    }
    screens_button(d, app_booth_button_rect(0), ox, 0, "BACK", 3, d->color, false);
    if (count) {
        screens_button(d, app_booth_button_rect(2), ox, 0, b->confirm_delete ? "SURE?" : "DELETE", 3,
                       b->confirm_delete ? WARN : d->dim, b->confirm_delete);
    }
    /* Free slots, in the middle button's place. */
    const app_rect_t mid = app_booth_button_rect(1);
    snprintf(text, sizeof(text), "PICS %u/%d", b->photos, APP_BOOTH_PHOTOS);
    px_text_center(text, mid.x + mid.w / 2 + ox, mid.y + 8, 2, d->dim, false);
    snprintf(text, sizeof(text), "GIFS %u/%d", b->gifs, APP_BOOTH_GIFS);
    px_text_center(text, mid.x + mid.w / 2 + ox, mid.y + 32, 2, d->dim, false);
}

/* Booth page without the store item: a camera and a way to the store. */
static void draw_teaser(const draw_ctx_t *d, int ox)
{
    center_text(d, "PHOTO BOOTH", ox, 128, 3, d->color);
    screens_camera_icon(d, 240 + ox - 60, 160, 120, d->color);
    px_text_center("SNAP PICS AND GIFS", 240 + ox, 268, 2, d->dim, false);
    px_text_center("OF YOU AND YOUR PAL", 240 + ox, 292, 2, d->dim, false);
    const app_store_item_t *item = app_game_store_item(app_game_store_find(APP_ITEM_GAME, APP_GAME_PHOTO));
    char text[24];
    snprintf(text, sizeof(text), "UNLOCK %u", item ? item->cost : 0);
    screens_button(d, app_booth_unlock_rect(), ox, 0, text, 3, d->accent, false);
    px_text_center("IN THE STORE", 240 + ox, 424, 2, d->dim, false);
}

void screens_draw_booth(const draw_ctx_t *d, int ox)
{
    const app_booth_snapshot_t *b = &d->snap->booth;
    if (!d->snap->camera_present) {
        view_message(d, ox, "PLUG CAMERA", "INTO LEFT SLOT", WARN);
        return;
    }
    if (!(d->snap->owned & APP_OWN_GAME_BIT(APP_GAME_PHOTO))) {
        draw_teaser(d, ox);
        return;
    }
    const app_rect_t r = app_booth_view_rect();
    const bool hot = b->phase == APP_BOOTH_COUNTDOWN || b->phase == APP_BOOTH_RECORD;
    draw_frame(r, ox, hot ? d->accent : d->color);
    switch (b->phase) {
    case APP_BOOTH_COUNTDOWN: draw_countdown(d, ox); break;
    case APP_BOOTH_RECORD: draw_record(d, ox); break;
    case APP_BOOTH_REVIEW: draw_review(d, ox); break;
    case APP_BOOTH_GALLERY: draw_gallery(d, ox); break;
    case APP_BOOTH_FLASH:
        draw_live_picture(d, ox);
        center_text(d, "SNAP!", ox, LABEL_Y, 3, d->accent);
        break;
    default: draw_live(d, ox); break;
    }
}

void screens_draw_booth_flash(const draw_ctx_t *d)
{
    const app_booth_snapshot_t *b = &d->snap->booth;
    if (d->snap->screen != APP_SCREEN_BOOTH || b->phase != APP_BOOTH_FLASH) {
        return;
    }
    const Color white = px_scale(PAPER, d->level);
    if (b->phase_ticks < FLASH_FULL_TICKS) {
        DrawRectangle(0, 0, APP_VIEW_W, APP_VIEW_H, white);
    } else {
        const app_rect_t r = app_booth_view_rect();
        DrawRectangle(r.x, r.y, r.w, r.h, white);
    }
}

/* ---- Lobby and store art ---------------------------------------------- */

void screens_camera_icon(const draw_ctx_t *d, int x, int y, int w, Color c)
{
    const int h = w * 2 / 3;
    px_box(x + w / 6, y, w / 3, h / 5, 0, c);                        /* flash hump */
    px_box(x, y + h / 6, w, h - h / 6, 0, c);                        /* body */
    px_box(x + w / 2 - h / 4, y + h / 6 + h / 8, h / 2, h / 2, 0, BLACK);   /* lens */
    px_box(x + w / 2 - h / 8, y + h / 6 + h / 4, h / 4, h / 4, 0, d->accent);
}

/* Store art for WEBCAM: the camera with a red ON AIR light. */
void screens_webcam_swatch(const draw_ctx_t *d, int x, int y, bool owned)
{
    const Color c = owned ? d->color : px_scale(d->color, 0.6f);
    screens_camera_icon(d, x, y + 10, 60, c);
    px_box(x + 46, y + 2, 12, 12, 0, WARN);
}

/* Store art for COLOR FILM: the 16 film colors. */
void screens_film_swatch(const draw_ctx_t *d, int x, int y, bool owned)
{
    Color pal[APP_PHOTO_COLORS];
    film_colors(owned ? d->level : 0.6f * d->level, pal);
    for (int i = 0; i < APP_PHOTO_COLORS; ++i) {
        DrawRectangle(x + (i % 4) * 15, y + (i / 4) * 15, 14, 14, pal[i]);
    }
}

/* Store art for a lens: the same square at its pixel density. */
void screens_lens_swatch(const draw_ctx_t *d, int res, int x, int y, bool owned)
{
    const int cells = 4 << res, px = 60 / cells;
    const Color c = owned ? d->color : px_scale(d->color, 0.6f);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            if ((i + j) & 1) {
                DrawRectangle(x + i * px, y + j * px, px, px, (i * j) % 3 ? c : d->accent);
            }
        }
    }
}

void screens_filter_swatch(const draw_ctx_t *d, int filter, int x, int y, bool owned)
{
    enum { N = 15, CELL = 4 };
    /* 450 bytes of locals: render-loop leaf on the 20 KB game task. */
    uint8_t luma[N * N], shades[N * N];
    for (int i = 0; i < N * N; ++i) {
        const int px = i % N, py = i / N, dx = px - 9, dy = py - 6;
        luma[i] = (uint8_t)(dx * dx + dy * dy < 12 ? 250 : 20 + (px + py) * 8);
    }
    app_photo_filter(luma, N, (app_photo_filter_t)filter, shades);   /* B/W art. */
    Color pal[APP_PHOTO_COLORS];
    palette_colors(filter, d->color, owned ? 1.0f : 0.6f, pal);
    draw_photo(shades, 0, N, N, x, y, CELL, pal);
}
