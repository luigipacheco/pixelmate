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
    case APP_ITEM_GAME: return s->owned & APP_OWN_GAME_BIT(item->value);
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

static void format_ip(char *out, size_t cap, const char *prefix, uint32_t ipv4)
{
    snprintf(out, cap, "%s%lu.%lu.%lu.%lu", prefix, (unsigned long)(ipv4 & 0xFF),
             (unsigned long)((ipv4 >> 8) & 0xFF), (unsigned long)((ipv4 >> 16) & 0xFF),
             (unsigned long)(ipv4 >> 24));
}

/* Network line: where to open the stats page, or a SET UP button when there
 * is nothing to join. Any state but OFF opens the Wi-Fi panel on tap. */
static void draw_menu_wifi(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    const app_rect_t r = app_menu_wifi_rect();
    const int y = r.y + oy + (r.h - 14) / 2;
    char text[40];
    switch (s->net) {
    case APP_NET_ONLINE:
        format_ip(text, sizeof(text), "WEB ", s->ipv4);
        px_text_center(text, 240, y, 2, d->color, false);
        break;
    case APP_NET_CONNECTING: px_text_center("WIFI CONNECTING", 240, y, 2, d->dim, false); break;
    case APP_NET_NO_CONFIG: screens_button(d, r, 0, oy, "SET UP WIFI", 2, d->color, false); break;
    case APP_NET_FAILED: screens_button(d, r, 0, oy, "WIFI LOST - SET UP", 2, d->color, false); break;
    case APP_NET_SETUP:
        snprintf(text, sizeof(text), "JOIN %s", s->setup_ap);
        px_text_center(text, 240, y, 2, d->accent, false);
        break;
    default: break;
    }
}

static void draw_menu(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    DrawRectangle(0, oy, APP_VIEW_W, APP_MENU_H, PANEL_BG);
    DrawRectangle(0, oy + APP_MENU_H - 4, APP_VIEW_W, 4, d->color);

    /* Status row: battery (left), SLEEP (center); sparks are drawn on top (right). */
    if (s->battery_pct >= 0) {
        char pct[8];
        px_battery(18, oy + 22, s->battery_pct, s->battery_charging, d->color, d->accent);
        snprintf(pct, sizeof(pct), "%d%%", s->battery_pct);
        px_text(pct, 60, oy + 23, 2, d->color, false);
    } else {
        px_text("NO BATT", 18, oy + 23, 2, d->dim, false);
    }
    screens_button(d, app_menu_sleep_rect(), 0, oy, "SLEEP", 3, d->color, false);

    const app_rect_t store = app_menu_store_rect(), style = app_menu_style_rect();
    const app_rect_t goal = app_menu_goal_rect();
    screens_button(d, store, 0, oy, "STORE", 4, d->color, true);
    px_icon(PX_ICON_BOLT, store.x + 26, store.y + oy + 15, 5, INK, INK, d->gap);
    screens_button(d, style, 0, oy, "STYLE", 4, d->color, false);
    px_icon(PX_ICON_EYE, style.x + 20, style.y + oy + 21, 4, d->color, d->accent, d->gap);
    screens_button(d, goal, 0, oy, "GOAL", 4, d->color, false);
    px_icon(PX_ICON_SHOE, goal.x + 22, goal.y + oy + 21, 4, d->color, d->accent, d->gap);

    char text[40];
    draw_menu_wifi(d, oy);
    const app_store_item_t *next = screens_next_unlock(s);
    if (!next) {
        snprintf(text, sizeof(text), "YOU OWN EVERYTHING");
    } else if (s->sparks >= next->cost) {
        snprintf(text, sizeof(text), "%s IS READY TO BUY", next->name);
    } else {
        snprintf(text, sizeof(text), "%s IN %lu STEPS", next->name,
                 (unsigned long)(next->cost - s->sparks) * APP_STEPS_PER_SPARK);
    }
    px_text_center(text, 240, oy + 326, 2, d->accent, false);
    draw_grabber(oy + APP_MENU_H - 18, d->dim);
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

/* SOUND button: a note with one or two waves for the level, crossed out when off. */
static void draw_sound_button(const draw_ctx_t *d, int oy)
{
    static const char *const names[APP_SOUND_COUNT] = {"ON", "LOUD", "OFF"};
    const app_sound_t sound = d->snap->sound < APP_SOUND_COUNT ? d->snap->sound : APP_SOUND_ON;
    const app_rect_t r = app_style_sound_rect();
    const bool muted = sound == APP_SOUND_OFF;
    const Color c = muted ? d->dim : d->color;
    const int x = r.x, y = r.y + oy;
    px_text_center("SOUND", x + r.w / 2, y - 20, 2, d->dim, false);
    px_box(x, y, r.w, r.h, 3, c);
    px_icon(PX_ICON_NOTE, x + 18, y + 22, 4, c, c, d->gap);
    if (muted) {
        for (int i = 0; i < 6; ++i) {
            DrawRectangle(x + 60 + i * 4, y + 24 + i * 4, 4, 4, d->accent);
            DrawRectangle(x + 80 - i * 4, y + 24 + i * 4, 4, 4, d->accent);
        }
    } else {
        for (int i = 0; i <= (int)sound; ++i) {
            DrawRectangle(x + 60 + i * 12, y + 30 - i * 6, 4, 20 + i * 12, d->accent);
        }
    }
    px_text_center(names[sound], x + r.w / 2, y + 76, 3, c, d->gap);
}

static void draw_style(const draw_ctx_t *d, int oy)
{
    static const char *const names[APP_PIXEL_COUNT] = {"CHUNKY", "FINE", "SMOOTH"};
    const app_game_snapshot_t *s = d->snap;
    draw_header(d, oy, "STYLE");
    draw_sound_button(d, oy);
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
    case APP_ITEM_GAME:
        if (item->value == APP_GAME_GRID) {
            /* A tiny 3x3 board with one dud. */
            for (int i = 0; i < 9; ++i) {
                const Color tile = i == 4 ? (Color){255, 48, 48, 255} : (i % 2 ? c : d->accent);
                px_box(x + (i % 3) * 20, y + (i / 3) * 20, 17, 17, 0, tile);
            }
            return;
        }
        /* Three tiny reels showing sevens. */
        for (int i = 0; i < APP_SLOT_REELS; ++i) {
            px_box(x + i * 20, y + 12, 18, 36, 2, c);
            px_text("7", x + i * 20 + 5, y + 23, 2, d->accent, false);
        }
        return;
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
        if (item->kind == APP_ITEM_GAME) {
            label = "PLAY";
            c = d->accent;
        } else if (item->kind == APP_ITEM_ANIM) {
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

/* ---- Daily goal ------------------------------------------------------ */

static void draw_goal(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    char text[24];
    draw_header(d, oy, "DAILY GOAL");
    snprintf(text, sizeof(text), "%lu", (unsigned long)s->step_goal);
    px_text_center(text, 240, oy + 92, 9, d->color, d->gap);
    px_text_center("STEPS A DAY", 240, oy + 168, 2, d->dim, false);

    const bool can_down = s->step_goal > APP_GOAL_MIN, can_up = s->step_goal < APP_GOAL_MAX;
    screens_button(d, app_goal_minus_rect(), 0, oy, "-", 8, can_down ? d->color : d->dim, false);
    screens_button(d, app_goal_plus_rect(), 0, oy, "+", 8, can_up ? d->color : d->dim, can_up);

    snprintf(text, sizeof(text), "TODAY %lu", (unsigned long)s->steps_today);
    px_text_center(text, 240, oy + 326, 3, s->steps_today >= s->step_goal ? d->accent : d->dim, d->gap);
}

/* ---- Wi-Fi ----------------------------------------------------------- */

static void draw_wifi(const draw_ctx_t *d, int oy)
{
    const app_game_snapshot_t *s = d->snap;
    const app_rect_t action = app_wifi_action_rect();
    const Color soft = px_scale(d->color, 0.6f);   /* Instructions: brighter than dim. */
    char text[40];
    draw_header(d, oy, "WIFI");
    switch (s->net) {
    case APP_NET_SETUP:
        px_text_center("ON YOUR PHONE JOIN", 240, oy + 84, 2, soft, false);
        px_text_center(s->setup_ap, 240, oy + 112, 4, d->color, d->gap);
        px_text_center("A SETUP PAGE OPENS. IF NOT GO TO", 240, oy + 176, 2, soft, false);
        px_text_center("192.168.4.1", 240, oy + 204, 4, d->color, d->gap);
        px_text_center("PICK YOUR WIFI AND SAVE", 240, oy + 270, 2, d->accent, false);
        px_text_center("NO PASSWORD FOR THIS HOTSPOT", 240, oy + 300, 2, soft, false);
        screens_button(d, action, 0, oy, "STOP", 3, soft, false);
        return;
    case APP_NET_ONLINE:
        px_text_center("ONLINE", 240, oy + 96, 4, d->accent, d->gap);
        px_text_center("STATS ON YOUR PHONE", 240, oy + 160, 2, soft, false);
        format_ip(text, sizeof(text), "", s->ipv4);
        px_text_center(text, 240, oy + 190, 4, d->color, d->gap);
        px_text_center("PIXELPAL.LOCAL", 240, oy + 246, 3, d->color, d->gap);
        screens_button(d, action, 0, oy, "CHANGE WIFI", 3, soft, false);
        return;
    case APP_NET_CONNECTING:
        px_text_center("CONNECTING", 240, oy + 140, 4, d->color, d->gap);
        px_text_center("TO THE SAVED WIFI", 240, oy + 196, 2, soft, false);
        screens_button(d, action, 0, oy, "CHANGE WIFI", 3, soft, false);
        return;
    case APP_NET_FAILED:
        px_text_center("WIFI NOT FOUND", 240, oy + 120, 4, d->color, d->gap);
        px_text_center("STILL TRYING THE SAVED ONE", 240, oy + 180, 2, soft, false);
        px_text_center("OR SET UP A NEW ONE", 240, oy + 206, 2, soft, false);
        screens_button(d, action, 0, oy, "SET UP", 3, d->color, true);
        return;
    case APP_NET_NO_CONFIG:
        px_text_center("NO WIFI YET", 240, oy + 120, 4, d->color, d->gap);
        px_text_center("SET IT UP WITH YOUR PHONE", 240, oy + 180, 2, soft, false);
        screens_button(d, action, 0, oy, "SET UP", 3, d->color, true);
        return;
    default:
        px_text_center("WIFI OFF", 240, oy + 140, 4, d->dim, d->gap);
        return;
    }
}

void screens_draw_overlay(const draw_ctx_t *d, app_overlay_t overlay, int oy)
{
    switch (overlay) {
    case APP_OVERLAY_MENU: draw_menu(d, oy); break;
    case APP_OVERLAY_STYLE: draw_style(d, oy); break;
    case APP_OVERLAY_STORE: draw_store(d, oy); break;
    case APP_OVERLAY_GOAL: draw_goal(d, oy); break;
    case APP_OVERLAY_WIFI: draw_wifi(d, oy); break;
    default: break;
    }
}
