// SPDX-License-Identifier: Apache-2.0
/* Pull-down panels: menu, style picker and store. */
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color INK = {12, 12, 12, 255};
static const Color PANEL_BG = {0, 0, 0, 255};
static const Color FRAME = {236, 236, 226, 255};

int screens_overlay_height(app_overlay_t overlay)
{
    return overlay == APP_OVERLAY_MENU ? APP_MENU_H : APP_VIEW_H;
}

static bool owns_item(const app_game_snapshot_t *s, const app_store_item_t *item)
{
    switch (item->kind) {
    case APP_ITEM_COLOR: return s->owned & APP_OWN_COLOR_BIT(item->value);
    case APP_ITEM_STYLE: return s->owned & APP_OWN_STYLE_BIT(item->value);
    default: return s->owned & APP_OWN_ANIM_BIT(item->value);
    }
}

const app_store_item_t *screens_next_unlock(const app_game_snapshot_t *s)
{
    const app_store_item_t *best = NULL;
    for (int i = 0; i < app_game_store_count(); ++i) {
        const app_store_item_t *item = app_game_store_item(i);
        if (!owns_item(s, item) && (!best || item->cost < best->cost)) {
            best = item;
        }
    }
    return best;
}

static void draw_grabber(int y, Color color)
{
    DrawRectangle(216, y, 48, 4, color);
    DrawRectangle(228, y + 8, 24, 4, color);
}

/* ---- Menu ------------------------------------------------------------ */

static void draw_menu(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    DrawRectangle(0, oy, APP_VIEW_W, APP_MENU_H, PANEL_BG);
    DrawRectangle(0, oy + APP_MENU_H - 4, APP_VIEW_W, 4, d->color);

    const app_rect_t store = app_menu_store_rect(), style = app_menu_style_rect();
    screens_button(d, store, 0, oy, "STORE", 5, d->color, true);
    px_icon(PX_ICON_BOLT, store.x + 24, store.y + oy + 17, 6, INK, INK, d->gap);
    screens_button(d, style, 0, oy, "STYLE", 5, d->color, false);
    px_icon(PX_ICON_EYE, style.x + 22, style.y + oy + 20, 5, d->color, d->accent, d->gap);

    const app_store_item_t *next = screens_next_unlock(s);
    char text[40];
    if (!next) {
        snprintf(text, sizeof(text), "YOU OWN EVERYTHING");
    } else if (s->sparks >= next->cost) {
        snprintf(text, sizeof(text), "%s IS READY TO BUY", next->name);
    } else {
        snprintf(text, sizeof(text), "%s IN %lu STEPS", next->name,
                 (unsigned long)(next->cost - s->sparks) * APP_STEPS_PER_SPARK);
    }
    px_text_center(text, 240, oy + 252, 2, d->accent, false);
    draw_grabber(oy + APP_MENU_H - 20, d->dim);
}

/* ---- Shared panel header --------------------------------------------- */

static void draw_header(const draw_ctx_t *d, int oy, const char *title)
{
    DrawRectangle(0, oy, APP_VIEW_W, APP_VIEW_H, PANEL_BG);
    screens_button(d, app_panel_back_rect(), 0, oy, "BACK", 3, d->dim, false);
    px_text_center(title, 240, oy + 20, 3, d->color, d->gap);
}

/* ---- Style ----------------------------------------------------------- */

static void draw_lock(int x, int y, Color color)
{
    px_icon(PX_ICON_LOCK, x, y, 3, color, color, false);
}

static void draw_style(const draw_ctx_t *d, int oy)
{
    static const char *const names[APP_PIXEL_COUNT] = {"CHUNKY", "FINE", "SMOOTH"};
    const app_game_snapshot_t *s = d->snap;
    draw_header(d, oy, "STYLE");
    px_text_center("PIXELS", 240, oy + 262, 2, d->dim, false);
    for (int i = 0; i < APP_PIXEL_COUNT; ++i) {
        const app_rect_t r = app_style_pixel_rect(i);
        const bool owned = s->owned & APP_OWN_STYLE_BIT(i);
        const bool on = s->pixel_style == (app_pixel_style_t)i;
        screens_button(d, r, 0, oy, names[i], 2, on ? d->color : d->dim, on);
        if (!owned) {
            draw_lock(r.x + r.w - 26, r.y + oy + 4, d->accent);
        }
    }
    px_text_center("COLOR", 240, oy + 350, 2, d->dim, false);
    for (int i = 0; i < APP_COLOR_COUNT; ++i) {
        const app_rect_t r = app_style_color_rect(i);
        const bool owned = s->owned & APP_OWN_COLOR_BIT(i);
        if (s->color_index == i) {
            px_box(r.x - 4, r.y + oy - 4, r.w + 8, r.h + 8, 3, FRAME);
        }
        px_box(r.x + 4, r.y + oy + 4, r.w - 8, r.h - 8, 0, owned ? px_color(i) : px_scale(px_color(i), 0.3f));
        if (!owned) {
            draw_lock(r.x + r.w / 2 - 10, r.y + oy + 16, FRAME);
        }
    }
}

/* ---- Store ----------------------------------------------------------- */

typedef struct {
    float cx, cy, hw, hh, r;
} mini_eye_t;

static bool mini_eye_inside(const void *shape, float x, float y)
{
    const mini_eye_t *e = shape;
    const float ax = x > e->cx ? x - e->cx : e->cx - x, ay = y > e->cy ? y - e->cy : e->cy - y;
    if (ax > e->hw || ay > e->hh) {
        return false;
    }
    const float qx = ax - (e->hw - e->r), qy = ay - (e->hh - e->r);
    return !(qx > 0 && qy > 0 && qx * qx + qy * qy > e->r * e->r);
}

static void draw_preview(const draw_ctx_t *d, const app_store_item_t *item, int x, int y, bool owned)
{
    const Color c = owned ? d->color : px_scale(d->color, 0.6f);
    switch (item->kind) {
    case APP_ITEM_COLOR:
        px_box(x, y, 60, 60, 0, px_color(item->value));
        return;
    case APP_ITEM_STYLE: {
        const mini_eye_t eye = {x + 30.0f, y + 30.0f, 24.0f, 30.0f, 10.0f};
        px_fill_shape(px_grid((app_pixel_style_t)item->value), x, y, x + 60, y + 61,
                      mini_eye_inside, &eye, c);
        return;
    }
    default:
        break;
    }
    switch ((app_anim_t)item->value) {
    case APP_ANIM_TRAIL:
        px_icon(PX_ICON_BOLT, x + 12, y + 2, 6, c, c, d->gap);
        return;
    case APP_ANIM_DANCE:
        px_icon(PX_ICON_NOTE, x + 6, y + 4, 7, c, c, d->gap);
        return;
    case APP_ANIM_STARS:
        px_icon(PX_ICON_STAR, x + 2, y + 4, 8, c, d->accent, d->gap);
        return;
    default:
        for (int i = 0; i < APP_COLOR_COUNT; ++i) {
            DrawRectangle(x + i * 10, y + 4, 8, 52, px_color(i));
        }
        return;
    }
}

static void draw_status(const draw_ctx_t *d, const app_store_item_t *item, app_rect_t r, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    const int right = r.x + r.w - 16, y = r.y + oy + 14;
    char text[12];
    if (owns_item(s, item)) {
        const char *label;
        Color c = d->color;
        if (item->kind == APP_ITEM_ANIM) {
            const bool on = s->anims_on & (1U << item->value);
            label = on ? "ON" : "OFF";
            c = on ? d->accent : d->dim;
        } else {
            const bool used = item->kind == APP_ITEM_COLOR ? s->color_index == item->value
                              : s->pixel_style == (app_pixel_style_t)item->value;
            label = used ? "IN USE" : "OWNED";
            c = used ? d->accent : d->dim;
        }
        px_text(label, right - px_text_width(label, 3), y, 3, c, d->gap);
        return;
    }
    const bool can = s->sparks >= item->cost;
    snprintf(text, sizeof(text), "%u", item->cost);
    const int tw = px_text_width(text, 3);
    px_text(text, right - tw, y, 3, can ? d->color : d->dim, d->gap);
    px_icon(PX_ICON_BOLT, right - tw - 22, y, 3, can ? d->accent : d->dim, d->accent, d->gap);
}

static void draw_store(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    draw_header(d, oy, "STORE");
    const int count = app_game_store_count();
    for (int row = 0; row < APP_STORE_PER_PAGE; ++row) {
        const int index = s->store_page * APP_STORE_PER_PAGE + row;
        const app_store_item_t *item = app_game_store_item(index);
        if (!item) {
            break;
        }
        const app_rect_t r = app_store_row_rect(row);
        const bool owned = owns_item(s, item);
        const bool can = owned || s->sparks >= item->cost;
        px_box(r.x, r.y + oy, r.w, r.h, 3, owned ? d->dim : (can ? d->color : px_scale(d->color, 0.4f)));
        draw_preview(d, item, r.x + 12, r.y + oy + 12, owned);
        px_text(item->name, r.x + 88, r.y + oy + 14, 3, can ? d->color : px_scale(d->color, 0.55f), d->gap);
        px_text(item->blurb, r.x + 88, r.y + oy + 50, 2, d->dim, false);
        draw_status(d, item, r, oy);
    }
    const int pages = (count + APP_STORE_PER_PAGE - 1) / APP_STORE_PER_PAGE;
    const int x0 = 240 - (pages - 1) * 12 - 5;
    for (int p = 0; p < pages; ++p) {
        if (p == s->store_page) {
            DrawRectangle(x0 + p * 24 - 2, oy + APP_DOTS_Y - 2, 14, 14, d->color);
        } else {
            DrawRectangle(x0 + p * 24, oy + APP_DOTS_Y, 10, 10, d->dim);
        }
    }
}

void screens_draw_overlay(const draw_ctx_t *d, app_overlay_t overlay, int oy)
{
    switch (overlay) {
    case APP_OVERLAY_MENU: draw_menu(d, oy); break;
    case APP_OVERLAY_STYLE: draw_style(d, oy); break;
    case APP_OVERLAY_STORE: draw_store(d, oy); break;
    default: break;
    }
}
