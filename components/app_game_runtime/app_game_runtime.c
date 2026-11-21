// SPDX-License-Identifier: Apache-2.0
/* Engine module adapter: input translation, fixed-step update, drawing and, on
 * the device only, persistence, the hardware pedometer and the calendar day. */
#include <stdio.h>
#include <string.h>
#include "include/app_game_runtime.h"
/* Engine Host accepts source lists, not component include paths. Reference only
 * the model's public header so these translation units work on both targets. */
#include "../app_game/include/app_game.h"
#include "private/game_config.h"
#include "private/pet_view.h"
#include "raylib_lite_raylib.h"

#if defined(RAYLIB_LITE_GAME_NATIVE)
#include <time.h>
#include "app_pedometer_hw.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "raylib_lite_clock.h"
#include "raylib_lite_save.h"

static const char *TAG = "pixel_pal";
enum {
    SAVE_VERSION = 1,
    SAVE_SOON_MS = 2000,          /* After spending, settings or a goal. */
    SAVE_LATER_MS = 30000,        /* After step/stat drift; limits flash wear. */
    SENSOR_POLL_TICKS = GAME_TICK_HZ,          /* 1 s */
    SENSOR_RETRY_TICKS = 5 * GAME_TICK_HZ,
    SENSOR_MAX_ATTEMPTS = 6,
    DAY_POLL_TICKS = 10 * GAME_TICK_HZ,
    VALID_EPOCH = 1700000000,     /* Earlier clocks are unsynchronized. */
};
#endif

/* Host builds: the space key ("jump" action) credits steps for quick testing. */
enum { GAME_ACTION_DEBUG_STEPS = 2, DEBUG_STEPS = 50 };

typedef struct {
    app_game_handle_t game;
    pet_view_t view;
    bool debug_key_down;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    raylib_lite_save_t save;
    bool save_ready;
    uint32_t saved_seq;
    uint64_t save_due_ms;
    bool sensor_ready;
    uint8_t sensor_attempts;
    uint32_t sensor_timer, day_timer;
#endif
} host_state_t;

#if defined(RAYLIB_LITE_GAME_NATIVE)
static uint64_t now_ms(void)
{
    return raylib_lite_time_us() / 1000U;
}

static void load_save(host_state_t *state)
{
    const esp_err_t nvs = nvs_flash_init();   /* No-op if already initialized. */
    if (nvs != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); progress will not persist", esp_err_to_name(nvs));
        return;
    }
    const raylib_lite_save_config_t config = {
        .storage = raylib_lite_save_nvs_storage(),
        .storage_namespace = "pixel_pal",
        .key = "pet",
        .version = SAVE_VERSION,
        .payload_size = sizeof(app_game_record_t),
        .debounce_ms = 0,
    };
    state->save_ready = raylib_lite_save_init(&state->save, &config) == RAYLIB_LITE_OK;
    if (!state->save_ready) {
        return;
    }
    app_game_record_t record;
    if (raylib_lite_save_load(&state->save, &record, NULL) == RAYLIB_LITE_OK) {
        app_game_import(state->game, &record);
        ESP_LOGI(TAG, "restored pet: %lu steps, %lu sparks",
                 (unsigned long)record.steps_total, (unsigned long)record.sparks);
    }
    state->saved_seq = app_game_read(state->game).save_seq;
}

static void maybe_save(host_state_t *state, uint32_t events)
{
    if (!state->save_ready) {
        return;
    }
    const uint64_t now = now_ms();
    const uint32_t seq = app_game_read(state->game).save_seq;
    if (seq != state->saved_seq) {
        const uint64_t due = now + ((events & APP_EVENTS_SAVE_NOW) ? SAVE_SOON_MS : SAVE_LATER_MS);
        if (!state->save_due_ms || due < state->save_due_ms) {
            state->save_due_ms = due;
        }
    }
    if (!state->save_due_ms || now < state->save_due_ms) {
        return;
    }
    app_game_record_t record;
    app_game_export(state->game, &record);
    if (raylib_lite_save_request(&state->save, &record, now) == RAYLIB_LITE_OK
            && raylib_lite_save_flush(&state->save, now, true) == RAYLIB_LITE_OK) {
        state->saved_seq = seq;
    }
    state->save_due_ms = 0;
}

static void poll_sensor(host_state_t *state)
{
    if (++state->sensor_timer < (state->sensor_ready ? SENSOR_POLL_TICKS : SENSOR_RETRY_TICKS)) {
        return;
    }
    state->sensor_timer = 0;
    if (!state->sensor_ready) {
        if (state->sensor_attempts >= SENSOR_MAX_ATTEMPTS) {
            return;
        }
        ++state->sensor_attempts;
        state->sensor_ready = app_pedometer_hw_start() == ESP_OK;
        ESP_LOGI(TAG, "hardware pedometer %s", state->sensor_ready ? "on" : "unavailable, using software");
        if (!state->sensor_ready) {
            return;
        }
    }
    uint32_t steps = 0;
    if (app_pedometer_hw_read(&steps) == ESP_OK) {
        app_game_set_sensor_steps(state->game, steps);
    }
}

static void poll_day(host_state_t *state)
{
    if (state->day_timer++ % DAY_POLL_TICKS) {
        return;
    }
    const time_t now = time(NULL);
    struct tm local;
    if (now < VALID_EPOCH || !localtime_r(&now, &local)) {
        return;
    }
    app_game_set_day(state->game, (local.tm_year - 100) * 400 + local.tm_yday + 1);
}
#endif

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    host_state_t *state = value;
    uint32_t seed = 0;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    seed = esp_random();
#endif
    const app_game_config_t config = {GAME_WIDTH, GAME_HEIGHT, seed};
    if (app_game_create(&config, &state->game) != APP_GAME_OK) {
        return -1;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    load_save(state);
#endif
    pet_view_init(&state->view);
    InitWindow(GAME_WIDTH, GAME_HEIGHT, GAME_NAME);
    SetTargetFPS(GAME_TICK_HZ);
    return 0;
}

static void shutdown(void *value)
{
    host_state_t *state = value;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (state->game && state->save_ready) {
        state->save_due_ms = 1;   /* Force a final flush. */
        maybe_save(state, 0);
    }
#endif
    app_game_destroy(&state->game);
}

static void input(void *value, const raylib_lite_game_input_v1_t *event)
{
    host_state_t *state = value;
    if (!event) {
        return;
    }
    switch (event->type) {
    case RAYLIB_LITE_GAME_INPUT_POINTER:
        if (event->track_id == 0) {
            app_game_set_pointer(state->game, event->x, event->y, event->pressed);
        }
        break;
    case RAYLIB_LITE_GAME_INPUT_IMU:
        app_game_add_accel(state->game, event->value_x, event->value_y, event->value_z);
        break;
    case RAYLIB_LITE_GAME_INPUT_CONTROL:
        switch (event->code) {
        case RAYLIB_LITE_GAME_CONTROL_PAUSE: app_game_set_paused(state->game, true); break;
        case RAYLIB_LITE_GAME_CONTROL_RESUME: app_game_set_paused(state->game, false); break;
        case RAYLIB_LITE_GAME_CONTROL_RESET: app_game_reset(state->game); break;
        default: break;
        }
        break;
    case RAYLIB_LITE_GAME_INPUT_ACTION:
        /* A board button never wipes the pet: only the explicit control resets it. */
        if (event->code == GAME_ACTION_PAUSE && event->pressed) {
            app_game_set_paused(state->game, !app_game_read(state->game).paused);
        }
#if !defined(RAYLIB_LITE_GAME_NATIVE)
        if (event->code == GAME_ACTION_DEBUG_STEPS) {
            if (event->pressed && !state->debug_key_down) {
                app_game_add_steps(state->game, DEBUG_STEPS);
            }
            state->debug_key_down = event->pressed;
        }
#endif
        break;
    default:
        break;
    }
}

static void update(void *value)
{
    host_state_t *state = value;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    poll_sensor(state);
    poll_day(state);
#endif
    app_game_update(state->game);
    const uint32_t events = app_game_take_events(state->game);
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    pet_view_update(&state->view, &snapshot, events);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    maybe_save(state, events);
#endif
}

static int render(void *value)
{
    host_state_t *state = value;
    if (!state->game) {
        return -1;
    }
    BeginDrawing();
    if (!raylib_lite_raylib_frame_available()) {
        EndDrawing();
        return -1;
    }
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    pet_view_draw(&state->view, &snapshot);
    EndDrawing();
    return 0;
}

static uint32_t state_hash(const void *value)
{
    const app_game_snapshot_t s = app_game_read(((const host_state_t *)value)->game);
    /* Hash named fields, avoiding platform-dependent struct padding. */
    uint32_t hash = s.tick;
    hash = hash * 31U + (uint32_t)s.screen;
    hash = hash * 31U + (uint32_t)s.mood;
    hash = hash * 31U + s.steps_total;
    hash = hash * 31U + s.sparks;
    hash = hash * 31U + s.hunger;
    hash = hash * 31U + s.happiness;
    hash = hash * 31U + s.friendship;
    hash = hash * 31U + (uint32_t)s.cards.phase;
    hash = hash * 31U + s.cards.pot;
    return hash * 31U + s.paused;
}

static int state_json(const void *value, char *output, size_t capacity)
{
    if (!output || !capacity) {
        return -1;
    }
    static const char *const screens[APP_SCREEN_COUNT] = {"home", "steps", "feed", "cards", "settings"};
    static const char *const moods[APP_MOOD_COUNT] = {
        "idle", "happy", "sleepy", "hungry", "sad", "walk", "eating", "thinking", "surprised", "grumpy"
    };
    const app_game_snapshot_t s = app_game_read(((const host_state_t *)value)->game);
    int size = snprintf(output, capacity,
                        "{\"phase\":\"%s\",\"tick\":%lu,\"screen\":\"%s\",\"mood\":\"%s\","
                        "\"steps_total\":%lu,\"steps_today\":%lu,\"sparks\":%lu,\"hunger\":%u,"
                        "\"happiness\":%u,\"friendship\":%u,\"pixel_style\":%d,\"color\":%u,"
                        "\"cards\":{\"phase\":%d,\"current\":%u,\"pot\":%u,\"streak\":%u,\"result\":%d},"
                        "\"pointer_x\":%ld,\"pointer_y\":%ld,\"pointer_down\":%s,\"state_hash\":\"%08lx\"}",
                        s.paused ? "paused" : "running", (unsigned long)s.tick,
                        screens[s.screen < APP_SCREEN_COUNT ? s.screen : 0],
                        moods[s.mood < APP_MOOD_COUNT ? s.mood : 0],
                        (unsigned long)s.steps_total, (unsigned long)s.steps_today,
                        (unsigned long)s.sparks, s.hunger, s.happiness, s.friendship,
                        (int)s.pixel_style, s.color_index, (int)s.cards.phase, s.cards.current,
                        s.cards.pot, s.cards.streak, (int)s.cards.result,
                        (long)s.pointer_x, (long)s.pointer_y,
                        s.pointer_down ? "true" : "false", (unsigned long)state_hash(value));
    return size < 0 || (size_t)size >= capacity ? -1 : size;
}

static const raylib_lite_game_module_v1_t s_module = {
    .descriptor = {
        RAYLIB_LITE_GAME_MODULE_ABI_V1, GAME_NAME, GAME_NAME,
        GAME_WIDTH, GAME_HEIGHT, GAME_TICK_HZ, 1
    },
    .state_size = sizeof(host_state_t),
    .initialize = initialize, .shutdown = shutdown, .input = input,
    .update = update, .render = render,
    .state_hash = state_hash, .state_json = state_json,
};

const raylib_lite_game_module_v1_t *app_game_runtime_module(void)
{
    return &s_module;
}
