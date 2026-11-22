// SPDX-License-Identifier: Apache-2.0
/* Engine module adapter: input translation, fixed-step update, drawing and, on
 * the device only, persistence, the hardware pedometer and the calendar day. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "include/app_game_runtime.h"
/* Engine Host accepts source lists, not component include paths. Reference only
 * the model's public header so these translation units work on both targets. */
#include "../app_game/include/app_game.h"
#include "private/booth_media.h"
#include "private/game_config.h"
#include "private/pet_view.h"
#include "raylib_lite_raylib.h"

#if defined(RAYLIB_LITE_GAME_NATIVE)
#include <stdlib.h>
#include <time.h>
#include "app_neck_hw.h"
#include "app_net.h"
#include "app_pedometer_hw.h"
#include "app_power_hw.h"
#include "app_sfx.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "private/pet_head.h"
#include "raylib_lite_clock.h"
#include "raylib_lite_save.h"

static const char *TAG = "pixel_pal";
enum {
    /* v1 builds could store an all-zero pet (ignored); v2..v5 migrate by prefix. */
    SAVE_VERSION = 6,
    SAVE_SOON_MS = 2000,          /* After spending, settings or a goal. */
    SAVE_LATER_MS = 30000,        /* After step/stat drift; limits flash wear. */
    SENSOR_POLL_TICKS = GAME_TICK_HZ,          /* 1 s */
    SENSOR_RETRY_TICKS = 5 * GAME_TICK_HZ,
    SENSOR_MAX_ATTEMPTS = 6,
    DAY_POLL_TICKS = 10 * GAME_TICK_HZ,
    BATTERY_POLL_TICKS = 10 * GAME_TICK_HZ,
    CHARGING_MIN_MA = 5,          /* Gauge current above this counts as charging. */
    BATTERY_LOG_POLLS = 6,        /* Log the power figures every 6th poll (1 min). */
    POWER_STATES = APP_POWER_DOZE + 1,
    VALID_EPOCH = 1700000000,     /* Earlier clocks are unsynchronized. */
    NET_START_TICK = GAME_TICK_HZ,             /* Start Wi-Fi 1 s after boot. */
    NET_POLL_TICKS = GAME_TICK_HZ,
    STATS_JSON_MAX = APP_NET_JSON_MAX,
};
#define NET_HOSTNAME "pixelmate"
#define TZ_NVS_NAMESPACE "pixel_pal"
#define TZ_NVS_KEY "tz_min"
#endif

/* Host builds: the space key ("jump" action) credits steps for quick testing,
 * and a fixed battery reading lets the battery UI show in the simulator. */
enum {
    GAME_ACTION_DEBUG_DAY = 0,    /* Host: left key ends the simulated day. */
    GAME_ACTION_DEBUG_CAMERA = 1, /* Host: right key unplugs/replugs the camera. */
    GAME_ACTION_DEBUG_STEPS = 2,
    GAME_ACTION_DEBUG_UNLOCK = 9, /* Host: replay action 9 owns everything (screenshots). */
    DEBUG_STEPS = 50,
    HOST_BATTERY_PCT = 76,
    HOST_FIRST_DAY = 20000,
    DIM_FRAME_EVERY = 3,          /* Dimmed screen: 10 frames a second instead of 30. */
};
#define HOST_SETUP_AP "PIXELMATE-SIM"   /* Host: stand-in setup hotspot name. */

typedef struct {
    app_game_handle_t game;
    pet_view_t view;
    booth_media_t media;          /* Photo booth pixels; the view reads them. */
    bool debug_key_down, debug_day_down, debug_camera_down;
    bool blanked;                 /* Black frame already submitted while the screen is off. */
    app_power_t drawn_power;      /* Power state of the last frame submitted. */
    uint32_t dim_ticks;           /* Render calls since the screen dimmed. */
    int32_t host_day;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    raylib_lite_save_t save;
    bool save_ready;
    uint32_t saved_seq;
    uint64_t save_due_ms;
    bool sensor_ready;
    uint8_t sensor_attempts;
    uint32_t sensor_timer, day_timer, battery_timer;
    app_battery_reading_t battery;     /* Last gauge reading, valid when battery_ok. */
    bool battery_ok;
    /* Discharge current per model power state since boot (mA, summed per poll),
     * so the cost of being awake, dim and asleep can be compared on the board. */
    uint32_t drain_sum[POWER_STATES], drain_polls[POWER_STATES];
    int logged_power;             /* Last model power state logged, for diagnostics. */
    uint8_t clock_logged;         /* 0 = not yet, 1 = logged invalid, 2 = logged valid. */
    uint32_t net_timer;
    bool net_started;
    char stats_json[STATS_JSON_MAX];   /* Instance-owned buffer for /api/stats. */
    uint32_t pocket_ticks;        /* Ticks the screen has been dark (pocket mode countdown). */
    bool pocket_radio_off;        /* Pocket mode suspended Wi-Fi. */
    pet_head_t head;              /* Head pose for boards with a neck (StackChan). */
#endif
} host_state_t;

#if defined(RAYLIB_LITE_GAME_NATIVE)
static uint64_t now_ms(void)
{
    return raylib_lite_time_us() / 1000U;
}

/* Pre-release photo-booth builds (feat/photo-booth before it was rebased on
 * Bit Flip) wrote their own 72-byte v5: looks/lenses at bytes 68..71 instead
 * of bits_best/reserved3, and the booth as store game 2, which is Bit Flip on
 * main. Main's v5 always stores reserved3 = 0, while those builds always set
 * the free LO RES lens bit at byte 70, so the two cannot be confused. */
static void migrate_booth_prerelease(const uint8_t *old, app_game_record_t *record)
{
    enum { OLD_PHOTO_GAME = 2 };
    record->filters_owned = old[68];
    record->filter = old[69];
    record->lenses_owned = old[70];
    record->lens = old[71];
    record->bits_best = 0;
    record->reserved3 = 0;
    if (record->owned & APP_OWN_GAME_BIT(OLD_PHOTO_GAME)) {
        record->owned &= ~APP_OWN_GAME_BIT(OLD_PHOTO_GAME);
        record->owned |= APP_OWN_GAME_BIT(APP_GAME_PHOTO);
    }
}

/* v2 (32 B), v3 (36 B), v4 (68 B) and v5 (72 B) are prefixes of the current
 * record: copy them and default the appended fields (v2 had no health, so it
 * starts full; before v6 no photo looks or lenses were owned). */
static raylib_lite_result_t migrate_save(uint16_t old_version, const void *old_data, size_t old_size,
                                        void *new_data, size_t new_size)
{
    const bool v2 = old_version == 2 && old_size == APP_GAME_RECORD_V2_SIZE;
    const bool v3 = old_version == 3 && old_size == APP_GAME_RECORD_V3_SIZE;
    const bool v4 = old_version == 4 && old_size == APP_GAME_RECORD_V4_SIZE;
    const bool v5 = old_version == 5 && old_size == APP_GAME_RECORD_V5_SIZE;
    if ((!v2 && !v3 && !v4 && !v5) || new_size != sizeof(app_game_record_t)) {
        return RAYLIB_LITE_INVALID_VERSION;
    }
    app_game_record_t *record = new_data;
    memset(record, 0, sizeof(*record));
    memcpy(record, old_data, old_size);
    if (v2) {
        record->hp = APP_HP_MAX;
    }
    if (v5 && ((const uint8_t *)old_data)[offsetof(app_game_record_t, reserved3)] != 0) {
        migrate_booth_prerelease(old_data, record);
    }
    return RAYLIB_LITE_OK;
}

/* Copy the stored "pet" blob, whatever it holds, to "pet_bak" before a fresh
 * pet may overwrite it. True when copied or when nothing is stored. */
static bool backup_unreadable_save(void)
{
    nvs_handle_t h;
    if (nvs_open("pixel_pal", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    size_t size = 0;
    const esp_err_t err = nvs_get_blob(h, "pet", NULL, &size);
    bool ok = err == ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_OK && size > 0) {
        uint8_t *raw = malloc(size);
        ok = raw && nvs_get_blob(h, "pet", raw, &size) == ESP_OK && nvs_set_blob(h, "pet_bak", raw, size) == ESP_OK
             && nvs_commit(h) == ESP_OK;
        free(raw);
    }
    nvs_close(h);
    return ok;
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
        .migrate = migrate_save,
    };
    state->save_ready = raylib_lite_save_init(&state->save, &config) == RAYLIB_LITE_OK;
    if (!state->save_ready) {
        return;
    }
    /* The loader reports success with the defaults when nothing is stored yet,
     * so the defaults must be the fresh pet, not zeros. */
    app_game_record_t fresh, record;
    app_game_export(state->game, &fresh);
    const raylib_lite_result_t loaded = raylib_lite_save_load(&state->save, &record, &fresh);
    if (loaded == RAYLIB_LITE_OK) {
        app_game_import(state->game, &record);
        ESP_LOGI(TAG, "restored pet: %lu steps, %lu sparks, hp %u",
                 (unsigned long)record.steps_total, (unsigned long)record.sparks, record.hp);
    } else if (!backup_unreadable_save()) {
        /* A save exists but can't be read (older firmware, torn write...) and
         * couldn't be backed up: never overwrite it with the fresh pet. */
        ESP_LOGE(TAG, "save unreadable (%d) and not backed up; saving disabled", (int)loaded);
        state->save_ready = false;
        return;
    } else {
        ESP_LOGE(TAG, "save unreadable (%d); raw copy kept in key \"pet_bak\"", (int)loaded);
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

/* Mean discharge in mA while in `power`, or -1 before any sample. */
static long mean_drain(const host_state_t *state, app_power_t power)
{
    return state->drain_polls[power] ? (long)(state->drain_sum[power] / state->drain_polls[power]) : -1L;
}

static void poll_battery(host_state_t *state)
{
    if (state->battery_timer++ % BATTERY_POLL_TICKS) {
        return;
    }
    app_battery_reading_t battery;
    state->battery_ok = app_power_hw_battery(&battery) == ESP_OK;
    if (!state->battery_ok) {
        app_game_set_battery(state->game, -1, false);
#if defined(APP_DESK_PET)
        app_game_set_keep_awake(state->game, false);
#endif
        return;
    }
    state->battery = battery;
    app_game_set_battery(state->game, battery.percent, battery.current_ma > CHARGING_MIN_MA);
#if defined(APP_DESK_PET)
    /* A desk pet on its charger stays awake: it is there to be looked at. */
    app_game_set_keep_awake(state->game, battery.external_power);
#endif
    const app_power_t power = app_game_read(state->game).power;
    if (battery.current_ma < 0 && (unsigned)power < POWER_STATES) {
        state->drain_sum[power] += (uint32_t)(-battery.current_ma);
        ++state->drain_polls[power];
    }
    if ((state->battery_timer / BATTERY_POLL_TICKS) % BATTERY_LOG_POLLS == 1) {
        ESP_LOGI(TAG, "battery %u%% %u mV, now %d mA, avg %d mA, %u/%u mAh, empty in %u min; "
                 "mean drain awake/dim/off %ld/%ld/%ld mA",
                 battery.percent, battery.voltage_mv, battery.current_ma, battery.avg_current_ma,
                 battery.remaining_mah, battery.full_mah, battery.time_to_empty_min,
                 mean_drain(state, APP_POWER_AWAKE), mean_drain(state, APP_POWER_DIM),
                 mean_drain(state, APP_POWER_DOZE));
    }
}

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). Unlike
 * year*400+yday it stays consecutive across New Year. */
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void poll_day(host_state_t *state)
{
    if (state->day_timer++ % DAY_POLL_TICKS) {
        return;
    }
    const time_t now = time(NULL);
    struct tm local;
    if (now < VALID_EPOCH || !localtime_r(&now, &local)) {
        if (!state->clock_logged) {
            state->clock_logged = true;
            ESP_LOGW(TAG, "clock not set (time=%lld); daily health is paused", (long long)now);
        }
        return;
    }
    if (state->clock_logged != 2) {
        state->clock_logged = 2;
        ESP_LOGI(TAG, "clock valid: %04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    }
    app_game_set_day(state->game, days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday));
}

/* ---- Wi-Fi, timezone and stats --------------------------------------- */

/* POSIX TZ from JavaScript's getTimezoneOffset(): minutes WEST of UTC, which is
 * also POSIX's sign convention ("UTC5:00" = five hours behind UTC). */
static void apply_timezone(int minutes_west)
{
    const int magnitude = abs(minutes_west);
    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%s%d:%02d", minutes_west < 0 ? "-" : "", magnitude / 60, magnitude % 60);
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "timezone %s", tz);
}

static void load_timezone(void)
{
    nvs_handle_t h;
    int32_t minutes = 0;
    if (nvs_open(TZ_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i32(h, TZ_NVS_KEY, &minutes) == ESP_OK) {
            apply_timezone((int)minutes);
        }
        nvs_close(h);
    }
}

static void save_timezone(int minutes)
{
    nvs_handle_t h;
    if (nvs_open(TZ_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_i32(h, TZ_NVS_KEY, minutes) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static int build_stats_json(const host_state_t *state, const app_game_snapshot_t *s, char *out, size_t cap)
{
    static const char *const moods[APP_MOOD_COUNT] = {
        "idle", "happy", "sleepy", "hungry", "sad", "walk", "eating", "thinking", "surprised", "grumpy",
        "fainted"
    };
    int n = snprintf(out, cap,
                     "{\"steps_today\":%lu,\"goal\":%lu,\"food_need\":%lu,\"fed_today\":%s,"
                     "\"fullness\":%u,\"hp\":%u,\"hp_max\":%d,\"fainted\":%s,\"streak\":%u,"
                     "\"steps_total\":%lu,\"sparks\":%lu,\"friendship\":%u,\"mood\":\"%s\","
                     "\"battery\":%d,\"charging\":%s,\"clock\":%s,\"tick\":%lu,\"power\":%d,\"history\":[",
                     (unsigned long)s->steps_today, (unsigned long)s->step_goal,
                     (unsigned long)s->food_need, s->fed_today ? "true" : "false", s->hunger, s->hp,
                     APP_HP_MAX, s->fainted ? "true" : "false", s->fed_streak,
                     (unsigned long)s->steps_total, (unsigned long)s->sparks, s->friendship,
                     moods[s->mood < APP_MOOD_COUNT ? s->mood : 0], s->battery_pct,
                     s->battery_charging ? "true" : "false", s->day > 0 ? "true" : "false",
                     (unsigned long)s->tick, (int)s->power);
    for (int i = 0; i < APP_HISTORY_DAYS && n > 0 && (size_t)n < cap; ++i) {
        n += snprintf(out + n, cap - (size_t)n, "%s%lu", i ? "," : "", (unsigned long)s->history[i]);
    }
    /* Power figures for measuring battery life (mA < 0 = discharging). */
    const app_battery_reading_t *b = &state->battery;
    if (n > 0 && (size_t)n < cap) {
        n += snprintf(out + n, cap - (size_t)n,
                      "],\"power_mv\":%u,\"power_ma\":%d,\"power_avg_ma\":%d,\"power_mah\":%u,"
                      "\"power_full_mah\":%u,\"power_tte_min\":%u,\"drain_ma\":[%ld,%ld,%ld]}",
                      state->battery_ok ? b->voltage_mv : 0U, state->battery_ok ? b->current_ma : 0,
                      state->battery_ok ? b->avg_current_ma : 0, state->battery_ok ? b->remaining_mah : 0U,
                      state->battery_ok ? b->full_mah : 0U, state->battery_ok ? b->time_to_empty_min : 0xFFFFU,
                      mean_drain(state, APP_POWER_AWAKE), mean_drain(state, APP_POWER_DIM),
                      mean_drain(state, APP_POWER_DOZE));
    }
    return n > 0 && (size_t)n < cap ? n : -1;
}

static void report_network(host_state_t *state)
{
    static const app_net_state_t map[] = {
        [APP_NET_STATUS_OFF] = APP_NET_OFF, [APP_NET_STATUS_NO_CONFIG] = APP_NET_NO_CONFIG,
        [APP_NET_STATUS_CONNECTING] = APP_NET_CONNECTING, [APP_NET_STATUS_ONLINE] = APP_NET_ONLINE,
        [APP_NET_STATUS_FAILED] = APP_NET_FAILED, [APP_NET_STATUS_SETUP] = APP_NET_SETUP,
    };
    uint32_t ip = 0;
    const app_net_status_t status = app_net_status(&ip);
    const bool known = (size_t)status < sizeof(map) / sizeof(map[0]);
    app_game_set_network(state->game, known ? map[status] : APP_NET_OFF, ip);
    app_game_set_setup_ap(state->game, status == APP_NET_STATUS_SETUP ? app_net_setup_name() : NULL);
}

static void poll_net(host_state_t *state)
{
    ++state->net_timer;
    if (!state->net_started) {
        if (state->net_timer >= NET_START_TICK) {
            state->net_started = true;
            app_net_start(NET_HOSTNAME);   /* Logs and reports NO_CONFIG itself. */
        }
        return;
    }
    int minutes_west = 0;
    if (app_net_take_timezone(&minutes_west)) {
        apply_timezone(minutes_west);
        save_timezone(minutes_west);
        state->day_timer = 0;              /* Re-evaluate the day right away. */
    }
    if (state->net_timer % NET_POLL_TICKS) {
        return;
    }
    report_network(state);
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    if (build_stats_json(state, &snapshot, state->stats_json, sizeof(state->stats_json)) > 0) {
        app_net_publish(state->stats_json);
    }
}
#endif

/* Menu/panel taps ask for the setup hotspot; the model never touches Wi-Fi.
 * The Host build stands in with a fake hotspot so the panels can be tried. */
static void handle_wifi_events(host_state_t *state, uint32_t events)
{
    if (!(events & (APP_EVENT_WIFI_SETUP | APP_EVENT_WIFI_STOP))) {
        return;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    const esp_err_t err = (events & APP_EVENT_WIFI_STOP) ? app_net_setup_stop() : app_net_setup_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi setup %s: %s", (events & APP_EVENT_WIFI_STOP) ? "stop" : "start",
                 esp_err_to_name(err));
    }
    report_network(state);   /* Show SETUP on this frame, not a second later. */
#else
    const bool start = !(events & APP_EVENT_WIFI_STOP);
    app_game_set_network(state->game, start ? APP_NET_SETUP : APP_NET_NO_CONFIG, 0);
    app_game_set_setup_ap(state->game, start ? HOST_SETUP_AP : NULL);
#endif
}

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    host_state_t *state = value;
    uint32_t seed = 0;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    seed = esp_random();
#endif
#if defined(APP_DESK_PET)
    const bool desk = true;    /* Set by the build for boards nobody walks with. */
#else
    const bool desk = false;
#endif
    const app_game_config_t config = {GAME_WIDTH, GAME_HEIGHT, seed, desk};
    if (app_game_create(&config, &state->game) != APP_GAME_OK) {
        return -1;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    state->logged_power = -1;
    load_save(state);
    load_timezone();
    pet_head_init(&state->head, seed);
    app_neck_hw_start();          /* Does nothing on boards without a neck. */
#else
    app_game_set_battery(state->game, HOST_BATTERY_PCT, false);
    app_game_set_network(state->game, APP_NET_NO_CONFIG, 0);   /* First run: nothing saved. */
    state->host_day = HOST_FIRST_DAY;
    app_game_set_day(state->game, state->host_day);
#endif
    booth_media_init(&state->media);
    pet_view_init(&state->view);
    state->view.media = &state->media;
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
    app_sfx_close();
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
#if defined(RAYLIB_LITE_GAME_NATIVE)
            const app_game_snapshot_t before = app_game_read(state->game);
            if (before.power != APP_POWER_AWAKE && before.pointer_down != event->pressed) {
                ESP_LOGI(TAG, "touch %s at %ld,%ld while screen %s", event->pressed ? "down" : "up",
                         (long)event->x, (long)event->y, before.power == APP_POWER_DIM ? "dim" : "off");
            }
#endif
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
        if (event->code == GAME_ACTION_DEBUG_CAMERA) {
            if (event->pressed && !state->debug_camera_down) {
                state->media.host_present = !state->media.host_present;
            }
            state->debug_camera_down = event->pressed;
        }
        if (event->code == GAME_ACTION_DEBUG_UNLOCK && event->pressed) {
            app_game_record_t r;
            app_game_export(state->game, &r);
            for (int i = 0; i < APP_COLOR_COUNT; ++i) r.owned |= APP_OWN_COLOR_BIT(i);
            for (int i = 0; i < APP_PIXEL_COUNT; ++i) r.owned |= APP_OWN_STYLE_BIT(i);
            for (int i = 0; i < APP_ANIM_COUNT; ++i) r.owned |= APP_OWN_ANIM_BIT(i);
            for (int i = 0; i < APP_GAME_COUNT; ++i) r.owned |= APP_OWN_GAME_BIT(i);
            r.filters_owned = 0xFF;
            r.lenses_owned = 0xFF;
            r.sparks += 1000U;
            app_game_import(state->game, &r);
        }
        if (event->code == GAME_ACTION_DEBUG_DAY) {
            if (event->pressed && !state->debug_day_down) {
                app_game_set_day(state->game, ++state->host_day);
            }
            state->debug_day_down = event->pressed;
        }
#endif
        break;
    default:
        break;
    }
}

#if defined(RAYLIB_LITE_GAME_NATIVE)
/* ---- Pocket mode --------------------------------------------------------
 * Once the screen has been dark a few seconds on battery, stop Wi-Fi and
 * light-sleep the chip. Nothing reboots, so the BMI270 keeps counting steps
 * on its own; the panel keeps its black frame (no panel commands: those
 * raced the board's frame task before). The AI button wakes the pet; a timer wakes it briefly to read steps, settle the day and save. */
enum {
    POCKET_AFTER_TICKS = 90,           /* 3 s of dark screen first. */
    POCKET_TIMER_S = 600,              /* Bookkeeping wake every 10 minutes. */
};
#define POCKET_BUTTON_GPIO GPIO_NUM_7  /* AI button, active low: the only wake key. */

#if defined(APP_POCKET_MODE)
static void pocket_resume_radio(host_state_t *state)
{
    if (state->pocket_radio_off) {
        state->pocket_radio_off = false;
        const esp_err_t err = app_net_suspend(false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "pocket: wifi resume: %s", esp_err_to_name(err));
        }
    }
}
#endif

static void pocket_maybe_sleep(host_state_t *state, const app_game_snapshot_t *s)
{
#if !defined(APP_POCKET_MODE)
    /* Only the ESP-Mosaico build has pocket mode: its wake button and battery
     * rule are that board's (see CMakeLists.txt). */
    (void)state;
    (void)s;
    return;
#else
    if (s->power != APP_POWER_DOZE || !state->blanked) {
        state->pocket_ticks = 0;
        pocket_resume_radio(state);
        return;
    }
    if (++state->pocket_ticks < POCKET_AFTER_TICKS) {
        return;
    }
    /* On USB power stay reachable: light sleep drops the USB link Iris uses.
     * A held button would wake us at once, so wait for its release. */
    if (!state->battery_ok || state->battery.current_ma >= 0 || gpio_get_level(POCKET_BUTTON_GPIO) == 0) {
        return;
    }
    if (!state->pocket_radio_off && app_net_suspend(true) == ESP_OK) {
        state->pocket_radio_off = true;
    }
    esp_sleep_enable_timer_wakeup((uint64_t)POCKET_TIMER_S * 1000000ULL);
    gpio_wakeup_enable(POCKET_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    const int64_t slept_at = esp_timer_get_time();
    esp_light_sleep_start();
    const int64_t slept_s = (esp_timer_get_time() - slept_at) / 1000000LL;
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    gpio_wakeup_disable(POCKET_BUTTON_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (!(causes & BIT(ESP_SLEEP_WAKEUP_GPIO))) {
        return;   /* Timer: this tick reads steps and the day; then back to sleep. */
    }
    ESP_LOGI(TAG, "pocket: AI button woke the pet after %lld s", slept_s);
    state->pocket_ticks = 0;
    app_game_wake(state->game);
    pocket_resume_radio(state);
#endif
}
#endif

static void update(void *value)
{
    host_state_t *state = value;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    poll_sensor(state);
    poll_day(state);
    poll_battery(state);
    poll_net(state);
#endif
    app_game_update(state->game);
    const Color pet = px_color(app_game_read(state->game).color_index);
    booth_media_update(&state->media, state->game, (app_photo_rgb_t) {pet.r, pet.g, pet.b});
    const uint32_t events = app_game_take_events(state->game);
    handle_wifi_events(state, events);
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    pet_view_update(&state->view, &snapshot, events);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if ((int)snapshot.power != state->logged_power) {
        state->logged_power = (int)snapshot.power;
        ESP_LOGI(TAG, "power state %d (pointer %s)", (int)snapshot.power,
                 snapshot.pointer_down ? "down" : "up");
    }
    maybe_save(state, events);
    app_sfx_update(events, &snapshot);
    /* Neck: asleep the head goes home and the servos let go. */
    pet_head_update(&state->head, &snapshot, events, NULL);
    app_neck_hw_aim(state->head.pan, state->head.tilt);
    app_neck_hw_rest(snapshot.power == APP_POWER_DOZE);
    pocket_maybe_sleep(state, &snapshot);
#endif
}

static int render(void *value)
{
    host_state_t *state = value;
    if (!state->game) {
        return -1;
    }
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    const bool screen_off = snapshot.power == APP_POWER_DOZE;
    /* Screen off: push one black frame (so nothing stale can stay visible even
     * if the panel ignores "off"), then submit nothing until it wakes. The
     * panel keeps the last frame, so a dimmed screen only needs every
     * DIM_FRAME_EVERY-th frame: less drawing and display traffic. */
    if (!screen_off) {
        state->blanked = false;
    } else if (state->blanked) {
        return 0;
    }
    if (snapshot.power != APP_POWER_DIM || state->drawn_power != APP_POWER_DIM) {
        state->dim_ticks = 0;
    } else if (++state->dim_ticks % DIM_FRAME_EVERY) {
        return 0;
    }
    BeginDrawing();
    if (!raylib_lite_raylib_frame_available()) {
        EndDrawing();
        return -1;
    }
    state->drawn_power = snapshot.power;
    if (screen_off) {
        ClearBackground(BLACK);
        state->blanked = true;
    } else {
        pet_view_draw(&state->view, &snapshot);
    }
    EndDrawing();
    return 0;
}

static uint32_t state_hash(const void *value)
{
    const app_game_snapshot_t s = app_game_read(((const host_state_t *)value)->game);
    /* Hash named fields, avoiding platform-dependent struct padding. */
    uint32_t hash = s.tick;
    hash = hash * 31U + (uint32_t)s.screen;
    hash = hash * 31U + (uint32_t)s.overlay;
    hash = hash * 31U + s.owned;
    hash = hash * 31U + s.anims_on;
    hash = hash * 31U + (uint32_t)s.mood;
    hash = hash * 31U + s.steps_total;
    hash = hash * 31U + s.sparks;
    hash = hash * 31U + s.hunger;
    hash = hash * 31U + s.happiness;
    hash = hash * 31U + s.friendship;
    hash = hash * 31U + (uint32_t)s.cards.phase;
    hash = hash * 31U + s.cards.pot;
    hash = hash * 31U + (uint32_t)s.booth.phase;
    hash = hash * 31U + s.filters_owned;
    return hash * 31U + s.paused;
}

static int state_json(const void *value, char *output, size_t capacity)
{
    if (!output || !capacity) {
        return -1;
    }
    static const char *const screens[APP_SCREEN_COUNT] = {"home", "steps", "feed", "arcade", "booth"};
    static const char *const overlays[APP_OVERLAY_COUNT] = {"none", "menu", "style", "store", "goal", "wifi", "settings", "about"};
    static const char *const nets[] = {"off", "no_config", "connecting", "online", "failed", "setup"};
    static const char *const moods[APP_MOOD_COUNT] = {
        "idle", "happy", "sleepy", "hungry", "sad", "walk", "eating", "thinking", "surprised", "grumpy",
        "fainted"
    };
    const app_game_snapshot_t s = app_game_read(((const host_state_t *)value)->game);
    int size = snprintf(output, capacity,
                        "{\"phase\":\"%s\",\"tick\":%lu,\"screen\":\"%s\",\"overlay\":\"%s\","
                        "\"store_page\":%u,\"owned\":%lu,\"anims_on\":%u,\"mood\":\"%s\","
                        "\"power\":%d,\"battery\":%d,\"charging\":%s,\"net\":\"%s\",\"setup_ap\":\"%s\","
                        "\"hp\":%u,\"fainted\":%s,\"fed_today\":%s,\"food_need\":%lu,"
                        "\"steps_total\":%lu,\"steps_today\":%lu,\"sparks\":%lu,\"hunger\":%u,"
                        "\"happiness\":%u,\"friendship\":%u,\"pixel_style\":%d,\"color\":%u,"
                        "\"cards\":{\"phase\":%d,\"current\":%u,\"pot\":%u,\"streak\":%u,\"result\":%d},"
                        "\"slots\":{\"phase\":%d,\"payout\":%u,\"line\":[%u,%u,%u]},"
                        "\"bits\":{\"phase\":%d,\"target\":%u,\"switches\":%u,\"hits\":%u,\"payout\":%u,\"best\":%u},"
                        "\"arcade\":%d,\"arcade_row\":%u,\"camera_present\":%s,\"filters_owned\":%u,"
                        "\"lenses_owned\":%u,\"booth\":{\"phase\":%d,\"filter\":%u,\"res\":%u,\"color\":%s,\"on_air\":%s,\"cam_code\":%u,\"unplugged\":%s,"
                        "\"camera\":%s,\"photos\":%u,\"gifs\":%u,\"index\":%u,\"result\":%d,"
                        "\"confirm\":%s},"
                        "\"pointer_x\":%ld,\"pointer_y\":%ld,\"pointer_down\":%s,\"state_hash\":\"%08lx\"}",
                        s.paused ? "paused" : "running", (unsigned long)s.tick,
                        screens[s.screen < APP_SCREEN_COUNT ? s.screen : 0],
                        overlays[s.overlay < APP_OVERLAY_COUNT ? s.overlay : 0],
                        s.store_page, (unsigned long)s.owned, s.anims_on,
                        moods[s.mood < APP_MOOD_COUNT ? s.mood : 0],
                        (int)s.power, s.battery_pct, s.battery_charging ? "true" : "false",
                        nets[(size_t)s.net < sizeof(nets) / sizeof(nets[0]) ? s.net : 0], s.setup_ap,
                        s.hp, s.fainted ? "true" : "false", s.fed_today ? "true" : "false",
                        (unsigned long)s.food_need,
                        (unsigned long)s.steps_total, (unsigned long)s.steps_today,
                        (unsigned long)s.sparks, s.hunger, s.happiness, s.friendship,
                        (int)s.pixel_style, s.color_index, (int)s.cards.phase, s.cards.current,
                        s.cards.pot, s.cards.streak, (int)s.cards.result,
                        (int)s.slots.phase, s.slots.payout,
                        app_game_slot_symbol(s.slots.pos_q[0], 0), app_game_slot_symbol(s.slots.pos_q[1], 0),
                        app_game_slot_symbol(s.slots.pos_q[2], 0),
                        (int)s.bits.phase, s.bits.target, s.bits.switches, s.bits.hits, s.bits.payout, s.bits.best,
                        (int)s.arcade, s.arcade_row, s.camera_present ? "true" : "false", s.filters_owned,
                        s.lenses_owned, (int)s.booth.phase, s.booth.filter, s.booth.res, s.booth.color ? "true" : "false", s.booth.on_air ? "true" : "false", s.booth.cam_code,
                        s.booth.unplugged ? "true" : "false",
                        s.booth.camera ? "true" : "false", s.booth.photos, s.booth.gifs, s.booth.index,
                        (int)s.booth.result, s.booth.confirm_delete ? "true" : "false",
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
