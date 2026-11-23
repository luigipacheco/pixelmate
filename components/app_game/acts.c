// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "include/app_acts.h"
#include "include/app_game.h"

static bool fail(char *error, size_t cap, const char *fmt, ...)
{
    if (error && cap) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error, cap, fmt, args);
        va_end(args);
    }
    return false;
}

/* ---- The built-in pack --------------------------------------------------- */

#define STEP(mood_, eyes_, color_, lx, ly, hop_, fx_, sound_, ticks_) \
    {.mood = (mood_), .eyes = (eyes_), .color = (color_), .look_x = (lx), .look_y = (ly), \
     .hop = (hop_), .fx = (fx_), .sound = (sound_), .ticks = (ticks_)}

static const app_act_t s_defaults[] = {
    {   /* A greeting whenever the pet wakes or starts. */
        .id = 1, .name = "HELLO", .text = "HI!", .trigger = APP_ACT_ON_WAKE, .cooldown_min = 1, .chance = 100,
        .step_count = 2,
        .steps = {
            STEP(APP_MOOD_SURPRISED, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, -10, 8, APP_ACT_FX_NONE, 0, 10),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, 0, 5, APP_ACT_FX_SPARKS, 1, 26),
        },
    },
    {   /* A morning stretch, once a day between 6:00 and 10:00. */
        .id = 2, .name = "MORNING", .text = "MORNING!", .trigger = APP_ACT_ON_TIME, .param = 6 * 60,
        .param2 = 10 * 60, .cooldown_min = 12 * 60, .chance = 100, .step_count = 3,
        .steps = {
            STEP(APP_MOOD_SLEEPY, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, 8, 0, APP_ACT_FX_ZZZ, 0, 36),
            STEP(APP_MOOD_SURPRISED, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, -12, 9, APP_ACT_FX_NONE, 4, 12),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_STARS, APP_ACT_COLOR_PET, 0, 0, 6, APP_ACT_FX_SPARKS, 2, 36),
        },
    },
    {   /* Left alone on the home screen: looks around, then sulks. */
        .id = 3, .name = "BORED", .trigger = APP_ACT_ON_IDLE, .param = 12, .cooldown_min = 5, .chance = 60,
        .step_count = 3,
        .steps = {
            STEP(APP_MOOD_THINKING, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, -30, 0, 0, APP_ACT_FX_NONE, 0, 22),
            STEP(APP_MOOD_THINKING, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 30, 0, 0, APP_ACT_FX_NONE, 0, 22),
            STEP(APP_MOOD_GRUMPY, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, 10, 0, APP_ACT_FX_ZZZ, 0, 34),
        },
    },
    {   /* Every fifth pat: heart eyes and a wiggle. */
        .id = 4, .name = "PURR", .text = "PURR", .trigger = APP_ACT_ON_PAT, .param = 5, .chance = 100,
        .step_count = 4,
        .steps = {
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_HEARTS, APP_ACT_COLOR_PET, 0, 0, 4, APP_ACT_FX_HEARTS, 1, 14),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_HEARTS, APP_ACT_COLOR_PET, -14, 0, 0, APP_ACT_FX_NONE, 0, 8),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_HEARTS, APP_ACT_COLOR_PET, 14, 0, 0, APP_ACT_FX_NONE, 0, 8),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_HEARTS, APP_ACT_COLOR_PET, 0, 0, 3, APP_ACT_FX_HEARTS, 0, 14),
        },
    },
    {   /* Now and then: a curious peek to each side. */
        .id = 5, .name = "PEEK", .trigger = APP_ACT_ON_RANDOM, .param = 3, .cooldown_min = 2, .chance = 100,
        .step_count = 3,
        .steps = {
            STEP(APP_ACT_MOOD_KEEP, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, -34, -6, 0, APP_ACT_FX_NONE, 0, 16),
            STEP(APP_ACT_MOOD_KEEP, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 34, -6, 0, APP_ACT_FX_NONE, 0, 16),
            STEP(APP_MOOD_HAPPY, APP_ACT_EYES_ROUND, APP_ACT_COLOR_PET, 0, 0, 4, APP_ACT_FX_NONE, 3, 12),
        },
    },
};

int app_acts_defaults(app_act_t *out, int max)
{
    const int count = (int)(sizeof(s_defaults) / sizeof(s_defaults[0]));
    const int n = count < max ? count : max;
    if (out && n > 0) {
        memcpy(out, s_defaults, (size_t)n * sizeof(app_act_t));
    }
    return n > 0 ? n : 0;
}

/* ---- Validation ---------------------------------------------------------- */

static bool name_ok(const char *s, size_t cap)
{
    size_t n = 0;
    for (; n < cap && s[n]; ++n) {
        const char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return n > 0 && n < cap;
}

static bool text_ok(const char *s, size_t cap)
{
    size_t n = 0;
    for (; n < cap && s[n]; ++n) {
        const char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '!' || c == '?'
                || c == '+' || c == '-' || c == '.')) {
            return false;
        }
    }
    return n < cap;
}

bool app_act_valid(const app_act_t *a, char *error, size_t cap)
{
    if (!a) {
        return fail(error, cap, "no act");
    }
    if (a->id == 0) {
        return fail(error, cap, "id must be 1..65535");
    }
    if (!name_ok(a->name, sizeof(a->name))) {
        return fail(error, cap, "act %u: name needs 1..%d of A-Z 0-9 _ -", a->id, APP_ACT_NAME_MAX - 1);
    }
    if (!text_ok(a->text, sizeof(a->text))) {
        return fail(error, cap, "%s: say allows up to %d of A-Z 0-9 space ! ? + - .", a->name, APP_ACT_TEXT_MAX - 1);
    }
    if (a->trigger >= APP_ACT_ON_COUNT) {
        return fail(error, cap, "%s: trigger must be 0..%d", a->name, APP_ACT_ON_COUNT - 1);
    }
    switch ((app_act_trigger_t)a->trigger) {
    case APP_ACT_ON_PAT:
        if (a->param < 1 || a->param > 1000) {
            return fail(error, cap, "%s: pat trigger needs param 1..1000", a->name);
        }
        break;
    case APP_ACT_ON_STEPS:
        if (a->param < 10 || a->param > 50000) {
            return fail(error, cap, "%s: steps trigger needs param 10..50000", a->name);
        }
        break;
    case APP_ACT_ON_IDLE:
        if (a->param < 3 || a->param > 600) {
            return fail(error, cap, "%s: idle trigger needs param 3..600 seconds", a->name);
        }
        break;
    case APP_ACT_ON_TIME:
        if (a->param >= 1440 || a->param2 >= 1440) {
            return fail(error, cap, "%s: time trigger needs minutes 0..1439", a->name);
        }
        if (a->cooldown_min < 60) {
            return fail(error, cap, "%s: time trigger needs a cooldown of 60+ minutes", a->name);
        }
        break;
    case APP_ACT_ON_RANDOM:
        if (a->param < 1 || a->param > 720) {
            return fail(error, cap, "%s: random trigger needs param 1..720 minutes", a->name);
        }
        break;
    default:
        break;
    }
    if (a->cooldown_min > APP_ACT_COOLDOWN_MAX) {
        return fail(error, cap, "%s: cooldown is at most %d minutes", a->name, APP_ACT_COOLDOWN_MAX);
    }
    if (a->chance > 100) {
        return fail(error, cap, "%s: chance is 0..100", a->name);
    }
    if (a->step_count < 1 || a->step_count > APP_ACT_STEPS_MAX) {
        return fail(error, cap, "%s: needs 1..%d steps", a->name, APP_ACT_STEPS_MAX);
    }
    for (int i = 0; i < a->step_count; ++i) {
        const app_act_step_t *s = &a->steps[i];
        if (s->mood != APP_ACT_MOOD_KEEP && s->mood > APP_MOOD_GRUMPY) {
            return fail(error, cap, "%s step %d: mood must be 0..%d or 255", a->name, i + 1, APP_MOOD_GRUMPY);
        }
        if (s->eyes >= APP_ACT_EYES_COUNT) {
            return fail(error, cap, "%s step %d: eyes must be 0..%d", a->name, i + 1, APP_ACT_EYES_COUNT - 1);
        }
        if (s->color >= APP_ACT_COLOR_FIRST + APP_COLOR_COUNT) {
            return fail(error, cap, "%s step %d: color must be 0..%d", a->name, i + 1,
                        APP_ACT_COLOR_FIRST + APP_COLOR_COUNT - 1);
        }
        if (s->look_x < -APP_ACT_LOOK_MAX || s->look_x > APP_ACT_LOOK_MAX || s->look_y < -APP_ACT_LOOK_MAX
                || s->look_y > APP_ACT_LOOK_MAX) {
            return fail(error, cap, "%s step %d: look must be -%d..%d", a->name, i + 1, APP_ACT_LOOK_MAX,
                        APP_ACT_LOOK_MAX);
        }
        if (s->hop > APP_ACT_HOP_MAX) {
            return fail(error, cap, "%s step %d: hop must be 0..%d", a->name, i + 1, APP_ACT_HOP_MAX);
        }
        if (s->fx >= APP_ACT_FX_COUNT) {
            return fail(error, cap, "%s step %d: fx must be 0..%d", a->name, i + 1, APP_ACT_FX_COUNT - 1);
        }
        if (s->sound > APP_ACT_SOUNDS) {
            return fail(error, cap, "%s step %d: sound must be 0..%d", a->name, i + 1, APP_ACT_SOUNDS);
        }
        if (s->ticks < 1 || s->ticks > APP_ACT_STEP_TICKS_MAX) {
            return fail(error, cap, "%s step %d: ticks must be 1..%d", a->name, i + 1, APP_ACT_STEP_TICKS_MAX);
        }
    }
    return true;
}

/* ---- Parsing ------------------------------------------------------------- */

static bool in_range(const long *v, int count, long lo, long hi)
{
    for (int i = 0; i < count; ++i) {
        if (v[i] < lo || v[i] > hi) {
            return false;
        }
    }
    return true;
}

int app_acts_parse(const char *text, app_act_t *out, int max, char *error, size_t cap)
{
    if (!text || !out || max <= 0) {
        fail(error, cap, "nothing to parse");
        return -1;
    }
    int count = 0, line_no = 0;
    app_act_t *cur = NULL;
    const char *p = text;
    while (*p) {
        char line[96];
        size_t n = 0;
        bool too_long = false;
        while (*p && *p != '\n') {
            if (n + 1 < sizeof(line)) {
                line[n++] = *p;
            } else {
                too_long = true;
            }
            ++p;
        }
        if (*p == '\n') {
            ++p;
        }
        ++line_no;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
            --n;
        }
        line[n] = '\0';
        const char *s = line;
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (!*s || *s == '#') {
            continue;
        }
        if (too_long) {
            fail(error, cap, "line %d is too long", line_no);
            return -1;
        }
        if (strncmp(s, "act ", 4) == 0) {
            if (count == max) {
                fail(error, cap, "at most %d acts", max);
                return -1;
            }
            long v[6];
            char name[APP_ACT_NAME_MAX];
            char extra;
            if (sscanf(s + 4, "%ld %11s %ld %ld %ld %ld %ld %c", &v[0], name, &v[1], &v[2], &v[3], &v[4], &v[5],
                       &extra) != 7 || !in_range(v, 6, 0, 65535)) {
                fail(error, cap, "line %d: act <id> <NAME> <trigger> <param> <param2> <cooldown> <chance>", line_no);
                return -1;
            }
            cur = &out[count++];
            memset(cur, 0, sizeof(*cur));
            cur->id = (uint16_t)v[0];
            snprintf(cur->name, sizeof(cur->name), "%s", name);
            cur->trigger = (uint8_t)(v[1] > 255 ? 255 : v[1]);
            cur->param = (uint16_t)v[2];
            cur->param2 = (uint16_t)v[3];
            cur->cooldown_min = (uint16_t)v[4];
            cur->chance = (uint8_t)(v[5] > 255 ? 255 : v[5]);
        } else if (strncmp(s, "say ", 4) == 0) {
            if (!cur) {
                fail(error, cap, "line %d: say before any act", line_no);
                return -1;
            }
            if (strlen(s + 4) >= sizeof(cur->text)) {
                fail(error, cap, "line %d: say is at most %d characters", line_no, APP_ACT_TEXT_MAX - 1);
                return -1;
            }
            snprintf(cur->text, sizeof(cur->text), "%s", s + 4);
        } else if (strncmp(s, "step ", 5) == 0) {
            if (!cur) {
                fail(error, cap, "line %d: step before any act", line_no);
                return -1;
            }
            if (cur->step_count == APP_ACT_STEPS_MAX) {
                fail(error, cap, "line %d: at most %d steps per act", line_no, APP_ACT_STEPS_MAX);
                return -1;
            }
            long v[9];
            char extra;
            if (sscanf(s + 5, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &extra) != 9 || !in_range(v, 9, -127, 255)) {
                fail(error, cap, "line %d: step needs 9 numbers", line_no);
                return -1;
            }
            if (v[0] < 0 || v[1] < 0 || v[2] < 0 || v[5] < 0 || v[6] < 0 || v[7] < 0 || v[8] < 0 || v[3] > 127
                    || v[4] > 127) {
                fail(error, cap, "line %d: step value out of range", line_no);
                return -1;
            }
            cur->steps[cur->step_count++] = (app_act_step_t) {
                .mood = (uint8_t)v[0], .eyes = (uint8_t)v[1], .color = (uint8_t)v[2], .look_x = (int8_t)v[3],
                .look_y = (int8_t)v[4], .hop = (uint8_t)v[5], .fx = (uint8_t)v[6], .sound = (uint8_t)v[7],
                .ticks = (uint8_t)v[8],
            };
        } else {
            fail(error, cap, "line %d: expected act, say or step", line_no);
            return -1;
        }
    }
    if (count == 0) {
        fail(error, cap, "no acts found");
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (!app_act_valid(&out[i], error, cap)) {
            return -1;
        }
        for (int j = 0; j < i; ++j) {
            if (out[j].id == out[i].id) {
                fail(error, cap, "id %u is used twice", out[i].id);
                return -1;
            }
        }
    }
    return count;
}

/* ---- JSON ---------------------------------------------------------------- */

int app_acts_to_json(const app_act_t *acts, int count, char *out, size_t cap)
{
    if (!out || !cap || (count > 0 && !acts)) {
        return -1;
    }
    size_t n = 0;
#define PUT(...) \
    do { \
        const int w_ = snprintf(out + n, cap - n, __VA_ARGS__); \
        if (w_ < 0 || (size_t)w_ >= cap - n) { \
            return -1; \
        } \
        n += (size_t)w_; \
    } while (0)
    PUT("{\"max_acts\":%d,\"max_steps\":%d,\"acts\":[", APP_ACTS_MAX, APP_ACT_STEPS_MAX);
    for (int i = 0; i < count; ++i) {
        const app_act_t *a = &acts[i];
        /* Names and texts only hold characters that need no JSON escaping. */
        PUT("%s{\"id\":%u,\"name\":\"%s\",\"say\":\"%s\",\"trigger\":%u,\"param\":%u,\"param2\":%u,"
            "\"cooldown\":%u,\"chance\":%u,\"steps\":[",
            i ? "," : "", a->id, a->name, a->text, a->trigger, a->param, a->param2, a->cooldown_min, a->chance);
        for (int k = 0; k < a->step_count; ++k) {
            const app_act_step_t *s = &a->steps[k];
            PUT("%s[%u,%u,%u,%d,%d,%u,%u,%u,%u]", k ? "," : "", s->mood, s->eyes, s->color, s->look_x, s->look_y,
                s->hop, s->fx, s->sound, s->ticks);
        }
        PUT("]}");
    }
    PUT("]}");
#undef PUT
    return (int)n;
}
