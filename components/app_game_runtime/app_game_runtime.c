// SPDX-License-Identifier: Apache-2.0
/* Engine module adapter: input translation, fixed-step update, drawing and, on
 * the device only, persistence, the hardware pedometer and the calendar day. */
#include <math.h>
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
#include "app_net.h"
#include "app_pedometer_hw.h"
#include "app_power_hw.h"
#include "app_sensors_hw.h"
#include "app_interact_hw.h"
#include "app_sleep_hw.h"
#include "app_sfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "raylib_lite_clock.h"
#include "raylib_lite_save.h"

static const char *TAG = "pixel_pal";

/* What pocket mode did, kept across a software restart (RTC memory) so a
 * stuck wake can be diagnosed afterwards: logged at boot and in /api/stats. */
#define POCKET_LOG_MAGIC 0x504B4C32U
#define POCKET_GRACE_TICKS (8 * GAME_TICK_HZ)   /* A pocket wake nobody follows up: back to sleep. */
typedef struct {
    uint32_t magic;
    uint32_t sleeps, timer_wakes, poll_wakes, button_wakes, stuck_restarts;
    uint32_t sleep_s, sleep_mah;   /* Deep sleep: time asleep and charge used (not charging). */
    uint32_t resleeps;             /* Woke, nobody touched it: straight back to sleep. */
} pocket_log_t;
static RTC_NOINIT_ATTR pocket_log_t s_pocket_log;
static volatile uint32_t s_frames_drawn;     /* Frames submitted (render). */

static void pocket_log_init(void)
{
    if (s_pocket_log.magic != POCKET_LOG_MAGIC) {
        memset(&s_pocket_log, 0, sizeof(s_pocket_log));
        s_pocket_log.magic = POCKET_LOG_MAGIC;
    }
    ESP_LOGI(TAG, "pocket log: %lu sleeps, %lu timer, %lu poll and %lu button wakes, %lu stuck restarts",
             (unsigned long)s_pocket_log.sleeps, (unsigned long)s_pocket_log.timer_wakes,
             (unsigned long)s_pocket_log.poll_wakes, (unsigned long)s_pocket_log.button_wakes,
             (unsigned long)s_pocket_log.stuck_restarts);
}

enum {
    /* v1 builds could store an all-zero pet (ignored); v2..v5 migrate by prefix. */
    SAVE_VERSION = 9,
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
    GAME_ACTION_DEBUG_USB = 8,    /* Host: replay action 8 toggles pretend USB power. */
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
    bool host_usb;                /* Host: action 8 pretends USB power (clock mode). */
    int clock_drawn_second;       /* Clock mode: second last drawn, -1 none. */
    int8_t new_year_cued;         /* New Year chime already played (weekday + 1). */
    uint32_t tool_ticks;          /* Ticks with a tool open. */
    uint8_t scan_seen;            /* Last tools.scan_seq acted on. */
    bool scan_pending;
    int mag_found;                /* Magnetometers that answered (-1 = not tried). */
    float mag_last[3];
    uint16_t metal_timer;
    /* Interaction module. */
    bool module;                  /* Present and up. */
    uint32_t module_timer;
    bool key_l, key_r, pir;       /* Last levels, for edges. */
    uint8_t light;
    uint8_t ir_seen;
    uint8_t ir_flash;
    uint8_t leds[6][3];
    bool leds_sent;
    bool host_module;             /* Host: action 7 pretends a module is plugged in. */
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
    bool pocket_deep_failed;      /* Deep sleep refused once: use the doze. */
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
    uint16_t pocket_after;        /* Dark ticks before pocket mode (shorter after the AI button). */
    uint16_t pocket_grace;        /* After a pocket wake: ticks left for a touch before sleeping again. */
    uint8_t ai_down_ticks;        /* AI button held this many ticks (debounce). */
    bool ai_swallow;              /* Ignore the AI button until it is released. */
    bool ai_ready;                /* GPIO input enabled. */
    bool sleep_reported;          /* The last test's result is on /api/sleep. */
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
    const bool v6 = old_version == 6 && old_size == APP_GAME_RECORD_V6_SIZE;
    const bool v7 = old_version == 7 && old_size == APP_GAME_RECORD_V7_SIZE;
    const bool v8 = old_version == 8 && old_size == APP_GAME_RECORD_V8_SIZE;
    if ((!v2 && !v3 && !v4 && !v5 && !v6 && !v7 && !v8) || new_size != sizeof(app_game_record_t)) {
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

/* Once the gauge has a reading after a restart: publish what the last sleep did. */
static void publish_sleep_report(host_state_t *state)
{
    if (state->sleep_reported || !state->battery_ok) {
        return;
    }
    state->sleep_reported = true;
    app_sleep_report_t r;
    if (!app_sleep_hw_report(&r)) {
        return;
    }
    /* Charge used while asleep, for the sleep current (this boot's wake only,
     * and only when it was not charging meanwhile). */
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP && r.woke && r.mah_before
            && state->battery.remaining_mah <= r.mah_before) {
        s_pocket_log.sleep_s += r.slept_s;
        s_pocket_log.sleep_mah += (uint32_t)(r.mah_before - state->battery.remaining_mah);
    }
    char alive[64] = "";
    size_t used = 0;
    for (int i = 0; i < app_sleep_probe_count && used + 6 < sizeof(alive); ++i) {
        if (r.alive_rail_off & (1U << i)) {
            used += (size_t)snprintf(alive + used, sizeof(alive) - used, "%s\"%02x\"", used ? "," : "",
                                     app_sleep_probe_addrs[i]);
        }
    }
    char json[600];
    const int n = snprintf(json, sizeof(json),
                           "{\"valid\":1,\"woke\":%d,\"reset_reason\":%u,\"wake_causes\":%lu,\"rail\":\"%s\","
                           "\"touch_wake\":%d,\"plain\":%d,\"mask\":%u,\"pins_57_60\":[%u,%u],\"from_crumb\":%d,\"board\":\"%s\",\"asked_s\":%lu,\"slept_s\":%lu,"
                           "\"steps_before\":%lu,\"steps_at_wake\":%ld,\"imu_answered_rail_off\":%d,"
                           "\"imu_chip_id\":%u,\"imu_status\":%u,\"alive_rail_off\":[%s],"
                           "\"mah_before\":%u,\"mah_after\":%u,\"mv_before\":%u,\"mv_after\":%u}",
                           r.woke, r.reset_reason, (unsigned long)r.wake_causes, r.rail_off ? "off" : "on",
                           r.touch_wake, r.plain, r.mask, r.pin57, r.pin60, r.from_crumb, r.v1_0 ? "v1.0" : "v1.1+", (unsigned long)r.asked_s,
                           (unsigned long)r.slept_s, (unsigned long)r.steps_before, (long)r.steps_at_wake,
                           r.imu_answered_rail_off, r.imu_chip_id, r.imu_status, alive, r.mah_before,
                           state->battery.remaining_mah, r.mv_before, state->battery.voltage_mv);
    ESP_LOGW(TAG, "sleep test result: %s", json);
    if (n > 0 && (size_t)n < sizeof(json)) {
        (void)app_net_publish_sleep(json);
    }
}

static const char *gauge_profile_name(app_gauge_profile_t p)
{
    static const char *const names[] = {"unchecked", "busy", "ok", "loaded", "failed"};
    return (unsigned)p < sizeof(names) / sizeof(names[0]) ? names[p] : "?";
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
    const esp_err_t err = app_power_hw_battery(&battery);
    if (err == ESP_ERR_NOT_FINISHED) {
        /* Gauge profile check still running (a moment at boot): keep what the
         * pet shows and ask again in a second instead of flashing NO BAT. */
        state->battery_timer = BATTERY_POLL_TICKS - GAME_TICK_HZ;
        return;
    }
    state->battery_ok = err == ESP_OK;
    if (!state->battery_ok) {
        app_game_set_battery(state->game, -1, false);
        return;
    }
    state->battery = battery;
    app_game_set_battery(state->game, battery.percent, battery.current_ma > CHARGING_MIN_MA);
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
                      "\"power_full_mah\":%u,\"power_design_mah\":%u,\"gauge_profile\":\"%s\","
                      "\"power_tte_min\":%u,\"drain_ma\":[%ld,%ld,%ld],"
                      "\"pocket\":[%lu,%lu,%lu,%lu,%lu],\"pocket_sleep\":[%lu,%lu,%lu]}",
                      state->battery_ok ? b->voltage_mv : 0U, state->battery_ok ? b->current_ma : 0,
                      state->battery_ok ? b->avg_current_ma : 0, state->battery_ok ? b->remaining_mah : 0U,
                      state->battery_ok ? b->full_mah : 0U, state->battery_ok ? b->design_mah : 0U,
                      gauge_profile_name(app_power_hw_profile()),
                      state->battery_ok ? b->time_to_empty_min : 0xFFFFU,
                      mean_drain(state, APP_POWER_AWAKE), mean_drain(state, APP_POWER_DIM),
                      mean_drain(state, APP_POWER_DOZE),
                      (unsigned long)s_pocket_log.sleeps, (unsigned long)s_pocket_log.timer_wakes,
                      (unsigned long)s_pocket_log.poll_wakes, (unsigned long)s_pocket_log.button_wakes,
                      (unsigned long)s_pocket_log.stuck_restarts, (unsigned long)s_pocket_log.sleep_s,
                      (unsigned long)s_pocket_log.sleep_mah, (unsigned long)s_pocket_log.resleeps);
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
    state->clock_drawn_second = -1;
    state->mag_found = -1;
    const app_game_config_t config = {GAME_WIDTH, GAME_HEIGHT, seed};
    if (app_game_create(&config, &state->game) != APP_GAME_OK) {
        return -1;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    state->logged_power = -1;
    load_save(state);
    pocket_log_init();
    uint32_t slept_steps = 0;
    if (app_sleep_hw_take_steps(&slept_steps)) {
        ++s_pocket_log.timer_wakes;   /* Deep-sleep wakes (touch or backstop timer). */
        app_game_add_sensor_delta(state->game, slept_steps);
        ESP_LOGI(TAG, "pocket: woke from deep sleep, %lu steps counted while asleep", (unsigned long)slept_steps);
        state->pocket_grace = POCKET_GRACE_TICKS;
    }
    load_timezone();
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
#if defined(RAYLIB_LITE_GAME_NATIVE)
            if (event->pressed) {
                state->pocket_grace = 0;   /* Someone is using it: stay awake. */
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
        if (event->code == 7 && event->pressed) {
            state->host_module = !state->host_module;   /* Host: pretend module. */
        }
        if (event->code == GAME_ACTION_DEBUG_USB && event->pressed) {
            state->host_usb = !state->host_usb;
        }
        if (event->code == GAME_ACTION_DEBUG_UNLOCK && event->pressed) {
            app_game_record_t r;
            app_game_export(state->game, &r);
            for (int i = 0; i < APP_COLOR_COUNT; ++i) r.owned |= APP_OWN_COLOR_BIT(i);
            for (int i = 0; i < APP_PIXEL_COUNT; ++i) r.owned |= APP_OWN_STYLE_BIT(i);
            for (int i = 0; i < APP_ANIM_COUNT; ++i) r.owned |= APP_OWN_ANIM_BIT(i);
            for (int i = 0; i < APP_GAME_COUNT; ++i) r.owned |= APP_OWN_GAME_BIT(i);
            r.filters_owned = 0xFF;
            r.filters_owned_hi = 0xFF;
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
 * Once the screen has been dark a few seconds on battery, save and send the
 * chip to deep sleep: the panel goes to Deep Standby, the 3.3 V rail stays on
 * so the BMI270 keeps counting steps and the touch controller can wake us. A
 * tap on the screen restarts the chip (the AI button cannot wake it: held at
 * boot it starts Vibe Mode); the steps counted meanwhile are read before the
 * board re-inits the sensor and credited at boot. A long backstop timer wakes
 * it if a tap never comes through. If deep sleep cannot start, the old deep
 * doze (game loop napping, AI button polled) takes over. */
enum {
    POCKET_AFTER_TICKS = 90,           /* 3 s of dark screen first. */
    POCKET_NAP_MS = 1000,              /* Deep doze: one game tick a second. */
    POCKET_POLL_MS = 20,               /* AI button check while napping. */
    POCKET_WATCH_MS = 3000,            /* No new frame this long after waking: restart. */
    POCKET_SOON_TICKS = 30,            /* After the AI button: pocket mode in 1 s. */
    POCKET_BACKSTOP_S = 12 * 60 * 60,  /* Deep sleep backstop wake (a tap wakes it sooner). */
};
#define POCKET_BUTTON_GPIO GPIO_NUM_7  /* AI button, active low: the only wake key. */




/* Save, credit the last steps and deep-sleep until a tap. */
static void pocket_deep_sleep(host_state_t *state)
{
    uint32_t raw = 0;
    const bool have_steps = app_pedometer_hw_read(&raw) == ESP_OK;
    if (have_steps) {
        app_game_set_sensor_steps(state->game, raw);   /* Credit what is counted so far. */
    }
    if (state->save_ready) {   /* The wake is a restart: save first. */
        app_game_record_t record;
        app_game_export(state->game, &record);
        const uint64_t now = now_ms();
        if (raylib_lite_save_request(&state->save, &record, now) == RAYLIB_LITE_OK
                && raylib_lite_save_flush(&state->save, now, true) == RAYLIB_LITE_OK) {
            state->saved_seq = app_game_read(state->game).save_seq;
        }
    }
    app_sfx_close();
    if (app_interact_hw_present()) {
        const uint8_t dark[APP_INTERACT_LEDS][3] = {{0}};
        (void)app_interact_hw_leds(dark);
    }
    /* The sensors that stay powered: the step sensor keeps only what step
     * counting needs (no gyroscope), the magnetometers suspend. */
    app_sensors_hw_mag_suspend();
    if (have_steps) {
        const esp_err_t lp = app_pedometer_hw_low_power();
        if (lp != ESP_OK) {
            ESP_LOGW(TAG, "pocket: step sensor low power: %s", esp_err_to_name(lp));
        }
    }
    const app_sleep_request_t request = {
        .seconds = POCKET_BACKSTOP_S,
        .touch_wake = true,
        .mask = APP_SLEEP_STEP_PANEL | APP_SLEEP_STEP_HOLD_RAIL | (have_steps ? APP_SLEEP_STEP_CAPTURE : 0),
        .steps = raw,
        .remaining_mah = state->battery_ok ? state->battery.remaining_mah : 0,
        .voltage_mv = state->battery_ok ? state->battery.voltage_mv : 0,
    };
    ++s_pocket_log.sleeps;
    (void)app_net_suspend(true);
    ESP_LOGI(TAG, "pocket: deep sleep (tap to wake), %lu steps so far", (unsigned long)raw);
    const esp_err_t err = app_sleep_hw_enter(&request);
    ESP_LOGE(TAG, "pocket: deep sleep did not start (%s): dozing instead", esp_err_to_name(err));
    (void)app_net_suspend(false);   /* Sound reopens on its own at the next cue. */
}

/* A pocket wake (a tap, maybe just the pocket) that nobody follows up with
 * another touch puts the pet straight back into pocket mode. */
static void pocket_grace_tick(host_state_t *state)
{
    if (!state->pocket_grace || --state->pocket_grace) {
        return;
    }
    if (!state->battery_ok || state->battery.current_ma >= 0) {
        return;   /* On USB: stay up. */
    }
    ++s_pocket_log.resleeps;
    ESP_LOGI(TAG, "pocket: woke but untouched, back to sleep");
    app_game_sleep(state->game);
    state->pocket_after = POCKET_SOON_TICKS;
}

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

static void pocket_maybe_sleep(host_state_t *state, const app_game_snapshot_t *s)
{
    if (s->power != APP_POWER_DOZE || !state->blanked) {
        state->pocket_ticks = 0;
        pocket_resume_radio(state);
        return;
    }
    if (++state->pocket_ticks < (state->pocket_after ? state->pocket_after : POCKET_AFTER_TICKS)) {
        return;
    }
    /* On USB power stay reachable for installs and the stats page. A held
     * button would wake us at once, so wait for its release. */
    if (!state->battery_ok || state->battery.current_ma >= 0 || gpio_get_level(POCKET_BUTTON_GPIO) == 0) {
        return;
    }
    if (!state->pocket_deep_failed) {
        pocket_deep_sleep(state);   /* Returns only if deep sleep could not start. */
        state->pocket_deep_failed = true;
    }
    if (!state->pocket_radio_off && app_net_suspend(true) == ESP_OK) {
        state->pocket_radio_off = true;
        ++s_pocket_log.sleeps;
    }
    /* Deep doze, not light sleep: on this ESP32-S31 IDF, esp_light_sleep_start
     * aborted in the clock driver (rtc_clk_apb_freq_get) and rebooted the board.
     * Instead the game loop naps here, so the CPU idles between ticks; the AI
     * button is checked every few milliseconds. Steps, the day and saving still
     * run on every (now rare) tick. */
    for (int i = 0; i < POCKET_NAP_MS / POCKET_POLL_MS; ++i) {
        vTaskDelay(pdMS_TO_TICKS(POCKET_POLL_MS));
        if (gpio_get_level(POCKET_BUTTON_GPIO) == 0) {
            ++s_pocket_log.button_wakes;
            ESP_LOGI(TAG, "pocket: AI button woke the pet");
            state->pocket_ticks = 0;
            state->pocket_after = 0;
            state->ai_swallow = true;   /* The press that woke us must not put it back to sleep. */
            app_game_wake(state->game);
            pocket_resume_radio(state);
            return;
        }
    }
    ++s_pocket_log.timer_wakes;
}
#endif

#if defined(RAYLIB_LITE_GAME_NATIVE)
/* The engine does not report the AI button on this board, so read it here:
 * a press puts the pet to sleep (pocket mode follows a second later on
 * battery), and a press while asleep wakes it, on battery or USB. */
static void poll_ai_button(host_state_t *state)
{
    if (!state->ai_ready) {
        gpio_input_enable(POCKET_BUTTON_GPIO);
        state->ai_ready = true;
    }
    if (gpio_get_level(POCKET_BUTTON_GPIO) != 0) {
        state->ai_down_ticks = 0;
        state->ai_swallow = false;
        return;
    }
    if (state->ai_down_ticks < UINT8_MAX) {
        ++state->ai_down_ticks;
    }
    if (state->ai_down_ticks != 2 || state->ai_swallow) {
        return;   /* Debounced: act once per press. */
    }
    state->ai_swallow = true;
    if (app_game_read(state->game).power == APP_POWER_DOZE) {
        ESP_LOGI(TAG, "AI button: wake");
        app_game_wake(state->game);
        state->pocket_after = 0;
    } else {
        ESP_LOGI(TAG, "AI button: sleep");
        app_game_sleep(state->game);
        state->pocket_after = POCKET_SOON_TICKS;
    }
}
#endif

/* Interaction module readings for the TOOLS sample. */
static void module_sample(host_state_t *state, pet_tool_sample_t *sample)
{
    if (state->module) {
        sample->light = state->light;
        sample->pir = state->pir;
    }
}

/* ---- Interaction module: presence, keys, PIR, IR and mood LEDs ---------- */

/* NEC codes: an LG-style TV remote, and a few common power codes for TV OFF. */
#if defined(RAYLIB_LITE_GAME_NATIVE)
static const uint8_t s_ir_codes[5][2] = {{0x04, 0x08}, {0x04, 0x02}, {0x04, 0x03}, {0x04, 0x00}, {0x04, 0x01}};
static const uint8_t s_ir_power[3][2] = {{0x04, 0x08}, {0x00, 0x45}, {0x40, 0x12}};
#endif

static void mood_leds(host_state_t *state, const app_game_snapshot_t *s, uint8_t out[6][3])
{
    memset(out, 0, 6 * 3);
    const app_tools_snapshot_t *t = &s->tools;
    const uint32_t tick = s->tick;
    if (t->flashlight) {
        memset(out, 255, 6 * 3);
        return;
    }
    if (t->guard_armed) {
        const bool alarm = state->pir && (tick / 4U) % 2U;
        for (int i = 0; i < 6; ++i) {
            out[i][0] = alarm ? 255 : (i == 0 ? 40 : 0);
        }
        return;
    }
    if (s->power == APP_POWER_DOZE || s->fainted || !s->module_lights) {
        return;   /* Asleep, or MODULE LIGHTS off: dark. */
    }
    const float breathe = 0.55f + 0.45f * sinf((float)tick * 0.07f);
    if (s->steps_today >= s->step_goal && s->step_goal) {
        for (int i = 0; i < 6; ++i) {   /* Goal reached: a rainbow chase. */
            const Color c = px_color((int)((tick / 6U + (uint32_t)i) % APP_COLOR_COUNT));
            out[i][0] = (uint8_t)(c.r / 3), out[i][1] = (uint8_t)(c.g / 3), out[i][2] = (uint8_t)(c.b / 3);
        }
        return;
    }
    Color c = px_color(s->color_index);
    if (s->mood == APP_MOOD_HUNGRY || (!s->fed_today && s->hunger < 25)) {
        c = (Color){255, 30, 30, 255};   /* Hungry: a red pulse. */
    }
    const float k = 0.25f * breathe;
    for (int i = 0; i < 6; ++i) {
        out[i][0] = (uint8_t)(c.r * k), out[i][1] = (uint8_t)(c.g * k), out[i][2] = (uint8_t)(c.b * k);
    }
}

static void run_module(host_state_t *state, const app_game_snapshot_t *s)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (state->module_timer++ % 60U == 0U) {   /* Hot-plug check every 2 s. */
        const bool present = app_interact_hw_poll();
        if (present != state->module) {
            state->module = present;
            state->leds_sent = false;
            app_game_set_module(state->game, present);
        }
    }
    if (!state->module) {
        return;
    }
    app_interact_inputs_t in;
    if (app_interact_hw_read(&in) != ESP_OK) {
        return;
    }
#else
    if (state->module != state->host_module) {
        state->module = state->host_module;
        app_game_set_module(state->game, state->module);
    }
    if (!state->module) {
        return;
    }
    struct { bool left, right, motion; uint8_t light; } in = {false, false, false, 60};
#endif
    if (in.left && !state->key_l) app_game_module_key(state->game, false);
    if (in.right && !state->key_r) app_game_module_key(state->game, true);
    if (in.motion && !state->pir) {
        if (s->tools.guard_armed) {
            app_game_guard_motion(state->game);
        } else if (s->power == APP_POWER_DOZE) {
            app_game_wake(state->game);   /* Someone walked by: say hi. */
        }
    }
    state->key_l = in.left, state->key_r = in.right, state->pir = in.motion, state->light = in.light;

    /* IR remote. */
    if (s->tools.ir_seq != state->ir_seen) {
        state->ir_seen = s->tools.ir_seq;
        state->ir_flash = 8;
#if defined(RAYLIB_LITE_GAME_NATIVE)
        const int b = s->tools.ir_button;
        if (b == 1) {
            for (int i = 0; i < 3; ++i) {   /* TV OFF: try a few common power codes. */
                (void)app_interact_hw_ir_nec(s_ir_power[i][0], s_ir_power[i][1]);
            }
        } else {
            const int k = b == 0 ? 0 : b - 1;
            (void)app_interact_hw_ir_nec(s_ir_codes[k][0], s_ir_codes[k][1]);
        }
#endif
    }
    if (state->ir_flash) {
        --state->ir_flash;
    }
    state->view.tools.ir_flash = state->ir_flash > 0;

    /* Mood LEDs, ~10 updates a second, only when something changed. */
    if (state->module_timer % 3U == 0U) {
        uint8_t leds[6][3];
        mood_leds(state, s, leds);
        if (!state->leds_sent || memcmp(leds, state->leds, sizeof(leds)) != 0) {
            memcpy(state->leds, leds, sizeof(leds));
            state->leds_sent = true;
#if defined(RAYLIB_LITE_GAME_NATIVE)
            (void)app_interact_hw_leds((const uint8_t (*)[3])leds);
#endif
        }
    }
#if !defined(RAYLIB_LITE_GAME_NATIVE)
    ++state->module_timer;
#endif
}

/* ---- TOOLS: sensor sampling (only while a tool is open) ---------------- */

static void feed_tools(host_state_t *state, const app_game_snapshot_t *s)
{
    const app_tools_snapshot_t *t = &s->tools;
    if (s->screen != APP_SCREEN_TOOLS || t->tool == APP_TOOL_LIST || s->power == APP_POWER_DOZE) {
        state->tool_ticks = 0;
        return;
    }
    const uint32_t tick = state->tool_ticks++;
    pet_tool_sample_t sample = {0};
    bool has_mag = false;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    (void)app_pedometer_hw_motion(sample.acc, sample.gyr);
    if (state->mag_found < 0 || (state->mag_found == 0 && tick % 150U == 0U)) {
        state->mag_found = app_sensors_hw_mag_start();   /* Retry now and then. */
    }
    if (state->mag_found > 0 && tick % 3U == 0U) {
        int16_t m[3];
        if (app_sensors_hw_mag_read(0, m) == ESP_OK) {
            state->mag_last[0] = m[0], state->mag_last[1] = m[1], state->mag_last[2] = m[2];
        }
    }
    has_mag = state->mag_found > 0;
    if (state->battery_ok) {
        sample.mv = state->battery.voltage_mv;
        sample.ma = state->battery.current_ma;
    }
#else
    /* Host: gentle made-up signals so every tool has something to draw. */
    const float ph = (float)tick / 30.0f;
    sample.acc[0] = 0.05f * sinf(ph * 2.0f);
    sample.acc[1] = 0.04f * cosf(ph * 1.3f);
    sample.acc[2] = 1.0f + 0.03f * sinf(ph * 7.0f);
    sample.gyr[0] = 40.0f * sinf(ph), sample.gyr[1] = 25.0f * cosf(ph * 0.7f), sample.gyr[2] = 10.0f;
    state->mag_last[0] = 300.0f * cosf(ph * 0.4f);
    state->mag_last[1] = 300.0f * sinf(ph * 0.4f);
    state->mag_last[2] = -200.0f;
    has_mag = true;
    sample.mv = 3950.0f + 20.0f * sinf(ph * 0.2f);
    sample.ma = -160.0f + 10.0f * sinf(ph);
    sample.light = 50.0f + 30.0f * sinf(ph * 0.5f);
#endif
    sample.mag[0] = state->mag_last[0], sample.mag[1] = state->mag_last[1], sample.mag[2] = state->mag_last[2];
    module_sample(state, &sample);
    pet_view_tool_sample(&state->view, s, &sample, has_mag);

    /* Metal detector: beep faster the stronger the field changes. */
    if (t->tool == APP_TOOL_COMPASS && t->metal_mode && has_mag) {
        const float dev = state->view.tools.metal_dev;
        const uint16_t every = (uint16_t)(dev > 300.0f ? 3 : 30 - (int)(dev / 11.0f));
        if (dev > 25.0f && ++state->metal_timer >= every) {
            state->metal_timer = 0;
            app_game_cue(state->game, APP_CUE_METAL);
        }
    }
    /* Wi-Fi scanner. */
    if (t->tool == APP_TOOL_WIFI && t->scan_seq != state->scan_seen) {
        state->scan_seen = t->scan_seq;
#if defined(RAYLIB_LITE_GAME_NATIVE)
        state->scan_pending = app_net_scan_start() == ESP_OK;
        pet_view_tool_aps(&state->view, NULL, state->scan_pending ? -1 : 0);
#else
        state->scan_pending = true;
        pet_view_tool_aps(&state->view, NULL, -1);
#endif
    }
    if (state->scan_pending && tick % 15U == 0U) {
        pet_tool_ap_t aps[PET_TOOL_APS];
#if defined(RAYLIB_LITE_GAME_NATIVE)
        app_net_ap_t found[PET_TOOL_APS];
        const int n = app_net_scan_results(found, PET_TOOL_APS);
        if (n >= 0) {
            for (int i = 0; i < n; ++i) {
                snprintf(aps[i].ssid, sizeof(aps[i].ssid), "%.19s", found[i].ssid);
                aps[i].rssi = found[i].rssi;
                aps[i].open = found[i].open;
            }
            state->scan_pending = false;
            pet_view_tool_aps(&state->view, aps, n);
        }
#else
        static const char *const names[4] = {"Neonet", "Coffee Shop", "Bitsy Lab", ""};
        static const int8_t rssi[4] = {-48, -63, -71, -84};
        for (int i = 0; i < 4; ++i) {
            snprintf(aps[i].ssid, sizeof(aps[i].ssid), "%s", names[i]);
            aps[i].rssi = rssi[i];
            aps[i].open = i == 1;
        }
        state->scan_pending = false;
        pet_view_tool_aps(&state->view, aps, 4);
#endif
    }
}

static void update(void *value)
{
    host_state_t *state = value;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    poll_ai_button(state);
    pocket_grace_tick(state);
    poll_sensor(state);
    poll_day(state);
    poll_battery(state);
    poll_net(state);
    publish_sleep_report(state);
#endif
    app_game_update(state->game);
    const Color pet = px_color(app_game_read(state->game).color_index);
    booth_media_update(&state->media, state->game, (app_photo_rgb_t) {pet.r, pet.g, pet.b});
    const uint32_t events = app_game_take_events(state->game);
    handle_wifi_events(state, events);
    const app_game_snapshot_t snapshot = app_game_read(state->game);
    pet_view_update(&state->view, &snapshot, events);
    run_module(state, &snapshot);
    feed_tools(state, &snapshot);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if ((int)snapshot.power != state->logged_power) {
        state->logged_power = (int)snapshot.power;
        ESP_LOGI(TAG, "power state %d (pointer %s)", (int)snapshot.power,
                 snapshot.pointer_down ? "down" : "up");
    }
    maybe_save(state, events);
    app_sfx_update(events, &snapshot);
    pocket_maybe_sleep(state, &snapshot);
#endif
}

/* Clock mode: the pet sleeps on outside power (USB) and the time is known.
 * The Host simulator stands in with action 8 and a fixed time. */
static bool clock_mode(host_state_t *state, pet_clock_t *out)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (!state->battery_ok || state->battery.current_ma < 0) {
        return false;   /* On battery: dark screen (and pocket mode) instead. */
    }
    const time_t now = time(NULL);
    struct tm t;
    if (now < VALID_EPOCH || !localtime_r(&now, &t)) {
        return false;
    }
    *out = (pet_clock_t) {t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday, t.tm_mday, t.tm_mon};
    return true;
#else
    if (!state->host_usb) {
        return false;
    }
    const uint32_t secs = app_game_read(state->game).tick / 30U;
    *out = (pet_clock_t) {10, (int)(41 + secs / 60U) % 60, (int)(secs % 60U), 6, 21, 10};
    return true;
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
    pet_clock_t clock;
    if (screen_off && clock_mode(state, &clock)) {
        /* Sleeping on USB power with the time known: a clock instead of black,
         * redrawn once a second (the panel keeps the frame in between). */
        if (clock.second == state->clock_drawn_second) {
            return 0;
        }
        BeginDrawing();
        if (!raylib_lite_raylib_frame_available()) {
            EndDrawing();
            return -1;
        }
        if (clock.month == 0 && clock.mday == 1 && clock.hour == 0 && clock.minute < 10
                && state->new_year_cued != clock.wday + 1) {
            state->new_year_cued = (int8_t)(clock.wday + 1);   /* Once per New Year. */
            app_game_cue(state->game, APP_CUE_NEW_YEAR);
        }
        pet_view_draw_clock(&state->view, &snapshot, &clock);
        EndDrawing();
        state->clock_drawn_second = clock.second;
        state->blanked = false;   /* Not black: pocket mode stays off. */
        state->drawn_power = snapshot.power;
#if defined(RAYLIB_LITE_GAME_NATIVE)
        ++s_frames_drawn;
#endif
        return 0;
    }
    state->clock_drawn_second = -1;
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
#if defined(RAYLIB_LITE_GAME_NATIVE)
    ++s_frames_drawn;
#endif
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
    static const char *const screens[APP_SCREEN_COUNT] = {"home", "steps", "feed", "arcade", "tools", "booth"};
    static const char *const overlays[APP_OVERLAY_COUNT] = {"none", "menu", "style", "store", "goal", "wifi", "settings", "about"};
    static const char *const nets[] = {"off", "no_config", "connecting", "online", "failed", "setup"};
    static const char *const moods[APP_MOOD_COUNT] = {
        "idle", "happy", "sleepy", "hungry", "sad", "walk", "eating", "thinking", "surprised", "grumpy",
        "fainted"
    };
    const app_game_snapshot_t s = app_game_read(((const host_state_t *)value)->game);
    char grid_tiles[APP_GRID_TILES + 1], grid_marks[APP_GRID_TILES + 1];
    for (int i = 0; i < APP_GRID_TILES; ++i) {
        grid_tiles[i] = (char)('0' + s.grid.tiles[i]);
        grid_marks[i] = (char)('0' + s.grid.marks[i]);
    }
    grid_tiles[APP_GRID_TILES] = grid_marks[APP_GRID_TILES] = 0;
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
                        "\"grid\":{\"phase\":%d,\"level\":%u,\"flips\":%u,\"score\":%u,\"payout\":%u,\"result\":%d,"
                        "\"revealed\":%lu,\"tiles\":\"%s\",\"marks\":\"%s\"},"
                        "\"says\":{\"phase\":%d,\"round\":%u,\"entered\":%u,\"lit\":%d,\"payout\":%u,\"best\":%u,\"failed\":%s},"
                        "\"stack\":{\"phase\":%d,\"rows\":%u,\"width\":%u,\"pos_q\":%d,\"dir\":%d,\"speed_q\":%u,"
                        "\"perfects\":%u,\"payout\":%u,\"best\":%u,\"top_left\":%u},"
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
                        (int)s.grid.phase, s.grid.level, s.grid.flips, s.grid.score, s.grid.payout, (int)s.grid.result,
                        (unsigned long)s.grid.revealed, grid_tiles, grid_marks,
                        (int)s.says.phase, s.says.round, s.says.entered, (int)s.says.lit, s.says.payout, s.says.best,
                        s.says.failed ? "true" : "false",
                        (int)s.stack.phase, s.stack.rows, s.stack.width, (int)s.stack.pos_q, (int)s.stack.dir,
                        s.stack.speed_q, s.stack.perfects, s.stack.payout, s.stack.best,
                        s.stack.rows ? s.stack.left[s.stack.rows - 1] : 0U,
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
