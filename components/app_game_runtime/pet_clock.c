// SPDX-License-Identifier: Apache-2.0
/* Clock mode: shown instead of a black screen while the pet sleeps on USB
 * power with the time known. Big pixel digits whose colon is a pair of tiny
 * Bitsy eyes that blink, glance and hop on the hour. Drawn once a second;
 * the panel keeps the frame in between. */
#include <math.h>
#include <stdio.h>
#include "private/pet_screens.h"

static const char *const DAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MONTHS[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

enum { DIGIT_SCALE = 13, DIGITS_Y = 168 };

/* One tiny eye: a block that squints shut to blink and shifts to glance. */
static void clock_eye(int cx, int cy, int dx, bool shut, Color c)
{
    if (shut) {
        DrawRectangle(cx - 12 + dx, cy - 2, 24, 5, c);
        return;
    }
    DrawRectangle(cx - 10 + dx, cy - 14, 20, 28, c);
    DrawRectangle(cx - 12 + dx, cy - 11, 24, 22, c);
}

void screens_draw_clock(const draw_ctx_t *d, const pet_clock_t *t)
{
    char text[24];
    /* Drift a few pixels each minute so nothing burns into the AMOLED. */
    const int ox = (t->minute % 7) - 3, oy = (t->minute % 5) - 2;
    const bool night = t->hour >= 22 || t->hour < 6;
    const Color ink = night ? px_scale(d->color, 0.6f) : d->color;

    snprintf(text, sizeof(text), "%s %s %d", DAYS[t->wday % 7], MONTHS[t->month % 12], t->mday);
    px_text_center(text, 240 + ox, 96 + oy, 3, d->dim, false);

    /* HH  MM with the eyes in between as the colon. */
    snprintf(text, sizeof(text), "%02d", t->hour);
    const int w = px_text_width(text, DIGIT_SCALE);
    px_text(text, 210 - w + ox, DIGITS_Y + oy, DIGIT_SCALE, ink, d->gap);
    snprintf(text, sizeof(text), "%02d", t->minute);
    px_text(text, 270 + ox, DIGITS_Y + oy, DIGIT_SCALE, ink, d->gap);

    const bool shut = night || (t->second % 6) == 0;           /* Blink, or sleep at night. */
    const int glance = (t->second / 10) % 3 == 1 ? -4 : ((t->second / 10) % 3 == 2 ? 4 : 0);
    const int hop = (t->minute == 0 && t->second < 15 && (t->second & 1)) ? -8 : 0;   /* Hourly hop. */
    clock_eye(240 + ox, DIGITS_Y + 24 + oy + hop, glance, shut, d->accent);
    clock_eye(240 + ox, DIGITS_Y + 72 + oy + hop, glance, shut, d->accent);
    if (night) {
        px_text("Z", 262 + ox, DIGITS_Y - 10 + oy - (t->second % 3) * 4, 2, d->dim, false);
    }

    /* Today's walk: steps and a goal bar (a desk pet is never walked). */
    const app_game_snapshot_t *s = d->snap;
    if (!s->desk) {
        snprintf(text, sizeof(text), "%lu STEPS", (unsigned long)s->steps_today);
        px_text_center(text, 240 + ox, 316 + oy, 3, d->color, false);
        const int bar = 240, done = s->step_goal ? (int)((uint64_t)bar * (s->steps_today > s->step_goal
                                                                           ? s->step_goal : s->steps_today) / s->step_goal) : 0;
        DrawRectangle(120 + ox, 350 + oy, bar, 8, d->dim);
        DrawRectangle(120 + ox, 350 + oy, done, 8, s->steps_today >= s->step_goal ? d->accent : d->color);
    }
    if (s->battery_pct >= 0) {
        snprintf(text, sizeof(text), "%s %d%%", s->battery_charging ? "CHARGING" : "POWER", s->battery_pct);
        px_text_center(text, 240 + ox, 380 + oy, 2, d->dim, false);
    }
    if (t->footer) {
        px_text_center(t->footer, 240 + ox, 420 + oy, 2, d->accent, false);   /* An instruction: readable. */
    } else {
        px_text_center("TAP TO WAKE", 240 + ox, 420 + oy, 2, px_scale(d->dim, 0.7f), false);
    }
    if (t->month == 0 && t->mday == 1 && t->hour == 0 && t->minute < 10) {
        /* Easter egg: New Year fireworks for the first ten minutes. */
        static const Color sparks[4] = {
            {255, 96, 96, 255}, {255, 220, 90, 255}, {120, 200, 255, 255}, {180, 255, 120, 255},
        };
        px_text_center("HAPPY NEW YEAR!", 240, 132, 3, d->accent, false);
        for (int b = 0; b < 3; ++b) {
            const unsigned h = (unsigned)(t->second * 7919 + b * 104729);
            const int cx = 60 + (int)(h % 360U), cy = 40 + (int)((h / 360U) % 90U);
            const int rad = 10 + (t->second + b * 3) % 5 * 8;
            for (int k = 0; k < 12; ++k) {
                const float a = (float)k * 0.5236f;
                DrawRectangle(cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad), 4, 4, sparks[(b + k) % 4]);
            }
        }
    }
}
