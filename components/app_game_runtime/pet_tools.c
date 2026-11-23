// SPDX-License-Identifier: Apache-2.0
/* TOOLS page: a Flipper-style grid of sensor apps. The runtime feeds sensor
 * samples into view->tools while a tool is open; the model only knows which
 * tool is open, the buttons, XP and the step calibration. */
#include <math.h>
#include <stdio.h>
#include "private/pet_screens.h"
#include "../app_game/include/app_game_layout.h"

static const Color WARN = {255, 48, 48, 255};
static const Color AXIS[3] = {{255, 96, 96, 255}, {110, 230, 110, 255}, {110, 160, 255, 255}};
static const char *const NAMES[APP_TOOL_COUNT] = {
    "", "SENSORS", "STEP CAL", "COMPASS", "LEVEL", "SEISMO", "WI-FI", "IR REMOTE", "GUARD", "LIGHT",
};

/* Graph area; round screens: narrower, inside the circle. */
#define GRAPH_X (app_layout_round ? 64 : 24)
#define GRAPH_Y 150
#define GRAPH_W (app_layout_round ? 352 : 432)
#define GRAPH_H (app_layout_round ? 176 : 200)

/* ---- Helpers ------------------------------------------------------- */

static const pet_tool_sample_t *sample_at(const pet_tools_view_t *t, int i)
{
    /* i = 0 oldest .. count-1 newest. */
    const int idx = (t->head + PET_TOOL_HIST - t->count + i) % PET_TOOL_HIST;
    return &t->hist[idx];
}

typedef float (*series_fn)(const pet_tool_sample_t *s, int k);

static void draw_series(const draw_ctx_t *d, int ox, series_fn fn, int k, float lo, float hi, Color c)
{
    const pet_tools_view_t *t = &d->view->tools;
    if (t->count < 2) {
        return;
    }
    int prev_y = 0;
    for (int i = 0; i < t->count; ++i) {
        float v = fn(sample_at(t, i), k);
        v = v < lo ? lo : (v > hi ? hi : v);
        const int x = GRAPH_X + ox + i * GRAPH_W / PET_TOOL_HIST;
        const int y = GRAPH_Y + GRAPH_H - 2 - (int)((v - lo) / (hi - lo) * (GRAPH_H - 4));
        if (i) {
            const int top = y < prev_y ? y : prev_y, bottom = y < prev_y ? prev_y : y;
            DrawRectangle(x - 1, top, 3, bottom - top + 3, c);
        }
        prev_y = y;
    }
}

static void graph_frame(const draw_ctx_t *d, int ox)
{
    px_box(GRAPH_X + ox - 4, GRAPH_Y - 4, GRAPH_W + 8, GRAPH_H + 8, 2, d->dim);
    DrawRectangle(GRAPH_X + ox, GRAPH_Y + GRAPH_H / 2, GRAPH_W, 1, px_scale(d->dim, 0.6f));
}

static float f_acc(const pet_tool_sample_t *s, int k) { return s->acc[k]; }
static float f_gyr(const pet_tool_sample_t *s, int k) { return s->gyr[k]; }
static float f_mag(const pet_tool_sample_t *s, int k) { return s->mag[k]; }
static float f_mv(const pet_tool_sample_t *s, int k) { (void)k; return s->mv; }
static float f_ma(const pet_tool_sample_t *s, int k) { (void)k; return s->ma; }
static float f_shake(const pet_tool_sample_t *s, int k)
{
    (void)k;
    return sqrtf(s->acc[0] * s->acc[0] + s->acc[1] * s->acc[1] + s->acc[2] * s->acc[2]) - 1.0f;
}
static float f_light(const pet_tool_sample_t *s, int k) { (void)k; return s->light; }

static const pet_tool_sample_t *latest(const draw_ctx_t *d)
{
    const pet_tools_view_t *t = &d->view->tools;
    return t->count ? sample_at(t, t->count - 1) : NULL;
}

static void title(const draw_ctx_t *d, int ox, const char *text)
{
    screens_button(d, app_tools_back_rect(), ox, 0, "BACK", 2, d->dim, false);
    /* Round: right of BACK, under the step and spark counters. */
    px_text_center(text, (app_layout_round ? 300 : 260) + ox, app_layout_round ? 102 : 74, 3, d->color, d->gap);
}

static void button(const draw_ctx_t *d, int ox, int i, const char *label, bool on)
{
    screens_button(d, app_tools_button_rect(i), ox, 0, label, 3, on ? d->accent : d->color, on);
}

static void note(const draw_ctx_t *d, int ox, int y, const char *text)
{
    px_text_center(text, 240 + ox, y, 2, d->dim, false);
}

/* ---- The grid ------------------------------------------------------ */

static void draw_icon(const draw_ctx_t *d, int tool, int cx, int cy, Color c)
{
    switch (tool) {
    case APP_TOOL_SENSORS:
        for (int i = 0; i < 8; ++i) {
            DrawRectangle(cx - 28 + i * 7, cy - (int)(sinf(i * 0.9f) * 10) - 2, 5, 5, c);
        }
        break;
    case APP_TOOL_STEPCAL:
        px_box(cx - 22, cy - 4, 14, 20, 0, c);
        px_box(cx + 6, cy - 16, 14, 20, 0, c);
        break;
    case APP_TOOL_COMPASS:
        px_box(cx - 18, cy - 18, 36, 36, 2, c);
        DrawRectangle(cx - 2, cy - 16, 4, 16, WARN);
        DrawRectangle(cx - 2, cy, 4, 14, c);
        break;
    case APP_TOOL_LEVEL:
        px_box(cx - 30, cy - 8, 60, 16, 2, c);
        DrawRectangle(cx - 6, cy - 4, 10, 8, d->accent);
        break;
    case APP_TOOL_SEISMO:
        for (int i = 0; i < 10; ++i) {
            const int h = (i == 4 || i == 5) ? 22 : (i % 2 ? 8 : 4);
            DrawRectangle(cx - 30 + i * 6, cy - h / 2, 4, h, c);
        }
        break;
    case APP_TOOL_WIFI:
        for (int i = 0; i < 4; ++i) {
            DrawRectangle(cx - 16 + i * 9, cy + 12 - i * 8, 6, 6 + i * 8, c);
        }
        break;
    case APP_TOOL_IR:
        px_box(cx - 12, cy - 18, 24, 36, 2, c);
        DrawRectangle(cx - 4, cy - 12, 8, 6, WARN);
        break;
    case APP_TOOL_GUARD:
        px_box(cx - 22, cy - 10, 44, 20, 2, c);
        DrawRectangle(cx - 6, cy - 6, 12, 12, d->accent);
        break;
    default:
        for (int i = 0; i < 8; ++i) {
            const float a = i * 0.785f;
            DrawRectangle(cx + (int)(cosf(a) * 16) - 2, cy + (int)(sinf(a) * 16) - 2, 5, 5, c);
        }
        DrawRectangle(cx - 6, cy - 6, 12, 12, d->accent);
        break;
    }
}

static void draw_list(const draw_ctx_t *d, int ox)
{
    const app_tools_snapshot_t *t = &d->snap->tools;
    char text[24];
    const int level = 1 + t->xp / APP_TOOLS_XP_PER_LEVEL;
    snprintf(text, sizeof(text), "TOOLS  BITSY LV %d", level);
    const int frac = t->xp % APP_TOOLS_XP_PER_LEVEL;
    if (app_layout_round) {   /* Centred under the counters, above the grid. */
        px_text_center(text, 240 + ox, 86, 3, d->color, d->gap);
        DrawRectangle(140 + ox, 112, 200, 6, d->dim);
        DrawRectangle(140 + ox, 112, 200 * frac / APP_TOOLS_XP_PER_LEVEL, 6, d->accent);
    } else {
        px_text(text, 24 + ox, 72, 3, d->color, d->gap);
        DrawRectangle(24 + ox, 104, 300, 8, d->dim);
        DrawRectangle(24 + ox, 104, 300 * frac / APP_TOOLS_XP_PER_LEVEL, 8, d->accent);
    }
    for (int i = 0; i < APP_TOOL_COUNT - 1; ++i) {
        const app_rect_t r = app_tools_cell_rect(i);
        const int tool = i + 1;
        const bool ok = tool < APP_TOOL_IR || t->module_present;
        const Color c = ok ? d->color : px_scale(d->dim, 0.8f);
        px_box(r.x + ox, r.y, r.w, r.h, 2, c);
        draw_icon(d, tool, r.x + ox + r.w / 2, r.y + 34, c);
        px_text_center(ok ? NAMES[tool] : "MODULE", r.x + ox + r.w / 2, r.y + r.h - 22, 2, c, false);
    }
}

/* ---- Tools --------------------------------------------------------- */

static void draw_sensors(const draw_ctx_t *d, int ox)
{
    static const char *const pages[APP_SENSOR_PAGES] = {
        "ACCEL G", "GYRO DEG/S", "MAGNET", "POWER", "STEPS",
    };
    const app_tools_snapshot_t *t = &d->snap->tools;
    const pet_tool_sample_t *now = latest(d);
    char text[40];
    title(d, ox, pages[t->sensor_page % APP_SENSOR_PAGES]);
    if (t->sensor_page != 4) {
        graph_frame(d, ox);
    }
    switch (t->sensor_page) {
    case 0:
        for (int k = 0; k < 3; ++k) draw_series(d, ox, f_acc, k, -2.0f, 2.0f, AXIS[k]);
        if (now) snprintf(text, sizeof(text), "X %.2f  Y %.2f  Z %.2f", now->acc[0], now->acc[1], now->acc[2]);
        break;
    case 1:
        for (int k = 0; k < 3; ++k) draw_series(d, ox, f_gyr, k, -250.0f, 250.0f, AXIS[k]);
        if (now) snprintf(text, sizeof(text), "X %.0f  Y %.0f  Z %.0f", now->gyr[0], now->gyr[1], now->gyr[2]);
        break;
    case 2:
        for (int k = 0; k < 3; ++k) draw_series(d, ox, f_mag, k, -600.0f, 600.0f, AXIS[k]);
        if (!d->view->tools.has_mag) {
            snprintf(text, sizeof(text), "NO MAGNETOMETER");
        } else if (now) {
            snprintf(text, sizeof(text), "X %.0f  Y %.0f  Z %.0f", now->mag[0], now->mag[1], now->mag[2]);
        }
        break;
    case 3:
        draw_series(d, ox, f_mv, 0, 3300.0f, 4300.0f, AXIS[1]);
        draw_series(d, ox, f_ma, 0, -400.0f, 400.0f, AXIS[0]);
        if (now) snprintf(text, sizeof(text), "%.0f MV   %.0f MA", now->mv, now->ma);
        break;
    default:
        snprintf(text, sizeof(text), "TODAY %lu", (unsigned long)d->snap->steps_today);
        px_text_center(text, 240 + ox, 180, 4, d->color, d->gap);
        snprintf(text, sizeof(text), "CALIBRATION %u%%", t->step_cal_pct ? t->step_cal_pct : 100U);
        px_text_center(text, 240 + ox, 250, 3, d->accent, false);
        snprintf(text, sizeof(text), "TOTAL %lu", (unsigned long)d->snap->steps_total);
        break;
    }
    if (!now && t->sensor_page != 4) {
        snprintf(text, sizeof(text), "WAITING FOR DATA");
    }
    px_text_center(text, 240 + ox, 364, 2, d->color, false);
    note(d, ox, 420, "TAP FOR THE NEXT SENSOR");
}

static void draw_stepcal(const draw_ctx_t *d, int ox)
{
    const app_tools_snapshot_t *t = &d->snap->tools;
    char text[32];
    title(d, ox, "STEP CAL");
    switch (t->cal_phase) {
    case APP_CAL_WALKING:
        note(d, ox, 140, "WALK 50 STEPS, COUNT THEM");
        snprintf(text, sizeof(text), "%u", t->cal_measured);
        px_text_center(text, 240 + ox, 190, 9, d->color, d->gap);
        note(d, ox, 300, "COUNTED BY THE SENSOR");
        note(d, ox, 330, "TAP DONE AT YOUR 50TH STEP");
        break;
    case APP_CAL_DONE:
        snprintf(text, sizeof(text), "%u%%", t->step_cal_pct);
        px_text_center(text, 240 + ox, 170, 9, d->accent, d->gap);
        snprintf(text, sizeof(text), "SENSOR SAW %u OF 50", t->cal_measured);
        note(d, ox, 280, text);
        note(d, ox, 310, "STEPS ARE NOW SCALED BY THIS");
        break;
    default:
        snprintf(text, sizeof(text), "NOW %u%%", t->step_cal_pct ? t->step_cal_pct : 100U);
        px_text_center(text, 240 + ox, 170, 6, d->color, d->gap);
        note(d, ox, 260, "TAP START, WALK EXACTLY 50 STEPS");
        note(d, ox, 290, "COUNTING THEM YOURSELF,");
        note(d, ox, 320, "THEN TAP DONE");
        break;
    }
    button(d, ox, 0, "START", t->cal_phase == APP_CAL_WALKING);
    button(d, ox, 1, "DONE", false);
    button(d, ox, 2, "RESET", false);
}

static void draw_compass(const draw_ctx_t *d, int ox)
{
    const pet_tools_view_t *v = &d->view->tools;
    const app_tools_snapshot_t *t = &d->snap->tools;
    const pet_tool_sample_t *now = latest(d);
    char text[32];
    title(d, ox, t->metal_mode ? "METAL" : "COMPASS");
    if (!v->has_mag || !now) {
        note(d, ox, 240, v->has_mag ? "WAITING FOR DATA" : "NO MAGNETOMETER FOUND");
        return;
    }
    if (t->metal_mode) {
        const float dev = v->metal_dev;
        const int w = (int)(dev > 400.0f ? 400.0f : dev);
        snprintf(text, sizeof(text), "%.0f", dev);
        px_text_center(text, 240 + ox, 170, 8, dev > 120.0f ? WARN : d->color, d->gap);
        const int bx = app_layout_round ? 80 : 40, bw = app_layout_round ? 320 : 400;
        DrawRectangle(bx + ox, 280, bw, 24, d->dim);
        DrawRectangle(bx + ox, 280, w * bw / 400, 24, dev > 120.0f ? WARN : d->accent);
        note(d, ox, 330, "MOVE NEAR METAL: IT BEEPS FASTER");
        note(d, ox, 420, "TAP FOR THE COMPASS");
        return;
    }
    const float cx = 0.5f * (v->mag_min[0] + v->mag_max[0]), cy = 0.5f * (v->mag_min[1] + v->mag_max[1]);
    const float span = fminf(v->mag_max[0] - v->mag_min[0], v->mag_max[1] - v->mag_min[1]);
    const float heading = atan2f(now->mag[1] - cy, now->mag[0] - cx);
    const int ccx = 240 + ox, ccy = 250, rad = 100;
    for (int i = 0; i < 36; ++i) {
        const float a = i * 0.1745f;
        DrawRectangle(ccx + (int)(cosf(a) * rad) - 2, ccy + (int)(sinf(a) * rad) - 2, 4, 4, d->dim);
    }
    /* Needle points to magnetic north: opposite the heading. */
    for (int r = -60; r <= 80; r += 4) {
        const float a = -heading - 1.5708f;
        DrawRectangle(ccx + (int)(cosf(a) * r) - 3, ccy + (int)(sinf(a) * r) - 3, 6, 6, r > 0 ? WARN : d->color);
    }
    int deg = (int)(heading * 57.2958f);
    deg = (deg + 360) % 360;
    static const char *const CARD[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    snprintf(text, sizeof(text), "%d DEG %s", deg, CARD[((deg + 22) / 45) % 8]);
    px_text_center(text, 240 + ox, 370, 3, d->color, false);
    note(d, ox, 420, span < 150.0f ? "SPIN ME FLAT TO CALIBRATE" : "TAP FOR THE METAL DETECTOR");
}

static void draw_level(const draw_ctx_t *d, int ox)
{
    const pet_tools_view_t *v = &d->view->tools;
    const pet_tool_sample_t *now = latest(d);
    char text[32];
    title(d, ox, "LEVEL");
    const int cx = 240 + ox, cy = 250, rad = 110;
    for (int i = 0; i < 48; ++i) {
        const float a = i * 0.1309f;
        DrawRectangle(cx + (int)(cosf(a) * rad) - 2, cy + (int)(sinf(a) * rad) - 2, 4, 4, d->dim);
    }
    px_box(cx - 18, cy - 18, 36, 36, 2, d->dim);
    if (!now) {
        note(d, ox, 380, "WAITING FOR DATA");
        return;
    }
    const float ax = now->acc[0] - v->zero_x, ay = now->acc[1] - v->zero_y;
    float bx = -ax * 220.0f, by = ay * 220.0f;
    const float r = sqrtf(bx * bx + by * by);
    if (r > rad - 14) {
        bx *= (rad - 14) / r;
        by *= (rad - 14) / r;
    }
    const bool flat = fabsf(ax) < 0.02f && fabsf(ay) < 0.02f;
    px_box(cx + (int)bx - 12, cy + (int)by - 12, 24, 24, 0, flat ? d->accent : d->color);
    snprintf(text, sizeof(text), "X %.1f  Y %.1f DEG", asinf(fmaxf(-1.0f, fminf(1.0f, ax))) * 57.3f,
             asinf(fmaxf(-1.0f, fminf(1.0f, ay))) * 57.3f);
    px_text_center(text, 240 + ox, 376, 3, flat ? d->accent : d->color, false);
    note(d, ox, 420, "TAP TO SET ZERO HERE");
}

static void draw_seismo(const draw_ctx_t *d, int ox)
{
    const pet_tool_sample_t *now = latest(d);
    title(d, ox, "SEISMO");
    graph_frame(d, ox);
    draw_series(d, ox, f_shake, 0, -0.6f, 0.6f, d->accent);
    float peak = 0.0f;
    const pet_tools_view_t *t = &d->view->tools;
    for (int i = t->count > 30 ? t->count - 30 : 0; i < t->count; ++i) {
        peak = fmaxf(peak, fabsf(f_shake(sample_at(t, i), 0)));
    }
    const char *word = peak < 0.03f ? "STILL" : peak < 0.15f ? "TREMOR" : peak < 0.6f ? "STOMP" : "QUAKE!";
    px_text_center(now ? word : "WAITING FOR DATA", 240 + ox, 370, 4, peak >= 0.6f ? WARN : d->color, d->gap);
}

static void draw_wifi(const draw_ctx_t *d, int ox)
{
    const pet_tools_view_t *v = &d->view->tools;
    char text[40];
    title(d, ox, "WI-FI SCAN");
    if (v->ap_count < 0) {
        note(d, ox, 220, (d->view->t / 10U) % 2U ? "SCANNING..." : "SCANNING");
    } else if (v->ap_count == 0) {
        note(d, ox, 220, "TAP SCAN TO LOOK AROUND");
    }
    for (int i = 0; i < v->ap_count && i < PET_TOOL_APS; ++i) {
        const pet_tool_ap_t *ap = &v->aps[i];
        const int y = (app_layout_round ? 144 : 124) + i * 32;
        const int bars = ap->rssi > -55 ? 4 : ap->rssi > -67 ? 3 : ap->rssi > -77 ? 2 : 1;
        for (int b = 0; b < 4; ++b) {
            DrawRectangle((app_layout_round ? 64 : 28) + ox + b * 7, y + 18 - b * 5, 5, 4 + b * 5,
                          b < bars ? d->accent : d->dim);
        }
        snprintf(text, sizeof(text), "%.16s", ap->ssid[0] ? ap->ssid : "(HIDDEN)");
        for (char *c = text; *c; ++c) {
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        }
        px_text(text, (app_layout_round ? 102 : 66) + ox, y + 4, 2, ap->open ? d->accent : d->color, false);
        snprintf(text, sizeof(text), "%d", ap->rssi);
        px_text(text, (app_layout_round ? 410 : 420) + ox - px_text_width(text, 2), y + 4, 2, d->dim, false);
    }
    button(d, ox, 1, "SCAN", v->ap_count < 0);
}

static void draw_ir(const draw_ctx_t *d, int ox)
{
    static const char *const LABELS[APP_IR_BUTTONS] = {"POWER", "TV OFF", "VOL +", "VOL -", "CH +", "CH -"};
    const app_tools_snapshot_t *t = &d->snap->tools;
    title(d, ox, "IR REMOTE");
    for (int i = 0; i < APP_IR_BUTTONS; ++i) {
        const bool hot = t->ir_button == i && d->view->tools.ir_flash;
        screens_button(d, app_tools_ir_rect(i), ox, 0, LABELS[i], 3, hot ? WARN : d->color, hot);
    }
    note(d, ox, 404, "NEC CODES: TVS AND CHEAP GADGETS");
}

static void draw_guard(const draw_ctx_t *d, int ox)
{
    const app_tools_snapshot_t *t = &d->snap->tools;
    const pet_tool_sample_t *now = latest(d);
    char text[32];
    title(d, ox, "GUARD");
    const bool alarm = now && now->pir && t->guard_armed;
    px_text_center(t->guard_armed ? "ARMED" : "OFF", 240 + ox, 150, 6,
                   alarm ? WARN : (t->guard_armed ? d->accent : d->dim), d->gap);
    snprintf(text, sizeof(text), "VISITORS %u", t->guard_hits);
    px_text_center(text, 240 + ox, 240, 3, d->color, false);
    note(d, ox, 290, now && now->pir ? "MOTION!" : "ALL QUIET");
    note(d, ox, 330, "BITSY WATCHES YOUR DESK");
    button(d, ox, 1, t->guard_armed ? "DISARM" : "ARM", t->guard_armed);
}

static void draw_light(const draw_ctx_t *d, int ox)
{
    const app_tools_snapshot_t *t = &d->snap->tools;
    const pet_tool_sample_t *now = latest(d);
    char text[24];
    title(d, ox, "LIGHT");
    graph_frame(d, ox);
    draw_series(d, ox, f_light, 0, 0.0f, 100.0f, d->accent);
    snprintf(text, sizeof(text), "%.0f%%", now ? now->light : 0.0f);
    px_text_center(text, 240 + ox, 364, 3, d->color, false);
    button(d, ox, 1, t->flashlight ? "LAMP OFF" : "LAMP ON", t->flashlight);
}

void screens_draw_tools(const draw_ctx_t *d, int ox)
{
    switch ((app_tool_t)d->snap->tools.tool) {
    case APP_TOOL_SENSORS: draw_sensors(d, ox); break;
    case APP_TOOL_STEPCAL: draw_stepcal(d, ox); break;
    case APP_TOOL_COMPASS: draw_compass(d, ox); break;
    case APP_TOOL_LEVEL: draw_level(d, ox); break;
    case APP_TOOL_SEISMO: draw_seismo(d, ox); break;
    case APP_TOOL_WIFI: draw_wifi(d, ox); break;
    case APP_TOOL_IR: draw_ir(d, ox); break;
    case APP_TOOL_GUARD: draw_guard(d, ox); break;
    case APP_TOOL_LIGHT: draw_light(d, ox); break;
    default: draw_list(d, ox); break;
    }
}
