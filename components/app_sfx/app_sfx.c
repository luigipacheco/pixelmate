// SPDX-License-Identifier: Apache-2.0
/* Event -> cue mapping for the pet's 8-bit sounds. One cue per tick, chosen by
 * priority (rewards over game feedback over UI clicks), plus at most one
 * delayed follow-up so a card flip can land before its win/lose sting. */
#include "app_sfx.h"

#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "raylib_lite_game_audio.h"

static const char *TAG = "pixel_pal_sfx";

typedef enum {
    SFX_NONE = 0,
    SFX_STEP, SFX_SPARK, SFX_COINS, SFX_FED, SFX_GOAL, SFX_PET, SFX_EAT, SFX_BUZZ,
    SFX_TAP, SFX_SWIPE, SFX_OPEN, SFX_CLOSE, SFX_BUY, SFX_DEAL, SFX_FLIP, SFX_WIN,
    SFX_LOSE, SFX_SPIN, SFX_REEL, SFX_JACKPOT, SFX_TILE, SFX_TILE_BONUS, SFX_ZAP,
    SFX_CLEAR, SFX_WAKE, SFX_FAINT, SFX_REVIVE,
    SFX_COUNT,
} sfx_t;

/* Packed names of assets_src/generate_sfx.py outputs. */
static const char *const SFX_PATHS[SFX_COUNT] = {
    [SFX_STEP] = "sfx_step.sound", [SFX_SPARK] = "sfx_spark.sound",
    [SFX_COINS] = "sfx_coins.sound", [SFX_FED] = "sfx_fed.sound",
    [SFX_GOAL] = "sfx_goal.sound", [SFX_PET] = "sfx_pet.sound",
    [SFX_EAT] = "sfx_eat.sound", [SFX_BUZZ] = "sfx_buzz.sound",
    [SFX_TAP] = "sfx_tap.sound", [SFX_SWIPE] = "sfx_swipe.sound",
    [SFX_OPEN] = "sfx_open.sound", [SFX_CLOSE] = "sfx_close.sound",
    [SFX_BUY] = "sfx_buy.sound", [SFX_DEAL] = "sfx_deal.sound",
    [SFX_FLIP] = "sfx_flip.sound", [SFX_WIN] = "sfx_win.sound",
    [SFX_LOSE] = "sfx_lose.sound", [SFX_SPIN] = "sfx_spin.sound",
    [SFX_REEL] = "sfx_reel.sound", [SFX_JACKPOT] = "sfx_jackpot.sound",
    [SFX_TILE] = "sfx_tile.sound", [SFX_TILE_BONUS] = "sfx_tile_bonus.sound",
    [SFX_ZAP] = "sfx_zap.sound", [SFX_CLEAR] = "sfx_clear.sound",
    [SFX_WAKE] = "sfx_wake.sound", [SFX_FAINT] = "sfx_faint.sound",
    [SFX_REVIVE] = "sfx_revive.sound",
};

enum {
    STEP_TICK_EVERY = 10,      /* Steps per soft tick (hardware steps arrive in bursts). */
    STING_DELAY_TICKS = 8,     /* Card flip, then its win/lose sting (~0.27 s at 30 Hz). */
    RESULT_DELAY_TICKS = 5,    /* Last reel clack, then the slot result. */
    JACKPOT_MIN_SPARKS = 30,   /* Same threshold as the model's long slot celebration. */
};

/* Mixer master gain per app_sound_t. Clip peaks are 0.1..0.5 full scale. */
static const float LEVEL_GAIN[APP_SOUND_COUNT] = {0.6f, 1.0f, 0.0f};

static struct {
    bool started;              /* Baseline taken on the first update. */
    bool audio_open, load_tried;
    Sound sounds[SFX_COUNT];
    app_sound_t level;
    app_power_t power;
    app_overlay_t overlay;
    bool fainted;
    uint32_t step_ticks;       /* steps_total / STEP_TICK_EVERY at the last tick. */
    sfx_t pending;
    uint8_t pending_ticks;
} s_sfx;

static void load_sounds(void)
{
    if (s_sfx.load_tried || !raylib_lite_game_audio_ready()) {
        return;
    }
    s_sfx.load_tried = true;
    int loaded = 0;
    for (int i = SFX_NONE + 1; i < SFX_COUNT; ++i) {
        s_sfx.sounds[i] = raylib_lite_game_audio_load_sound(SFX_PATHS[i]);
        if (s_sfx.sounds[i].frameCount) {
            ++loaded;
        } else {
            ESP_LOGW(TAG, "missing sound %s", SFX_PATHS[i]);
        }
    }
    ESP_LOGI(TAG, "%d/%d sounds loaded", loaded, SFX_COUNT - 1);
}

static void set_level(app_sound_t level)
{
    s_sfx.level = level < APP_SOUND_COUNT ? level : APP_SOUND_ON;
    raylib_lite_game_audio_set_master_volume(LEVEL_GAIN[s_sfx.level]);
}

static void play(sfx_t sfx)
{
    if (sfx != SFX_NONE && s_sfx.sounds[sfx].frameCount) {
        raylib_lite_game_audio_play_sound(s_sfx.sounds[sfx]);
    }
}

static sfx_t card_sting(app_card_result_t result)
{
    switch (result) {
    case APP_CARD_RESULT_WIN: return SFX_WIN;
    case APP_CARD_RESULT_LOSE: return SFX_LOSE;
    default: return SFX_NONE;
    }
}

static sfx_t slot_result(uint16_t payout)
{
    return payout >= JACKPOT_MIN_SPARKS ? SFX_JACKPOT : (payout ? SFX_WIN : SFX_LOSE);
}

static sfx_t grid_result(app_grid_result_t result)
{
    switch (result) {
    case APP_GRID_RESULT_CLEARED: return SFX_CLEAR;
    case APP_GRID_RESULT_CASHED: return SFX_COINS;
    case APP_GRID_RESULT_DUD: return SFX_ZAP;
    default: return SFX_NONE;
    }
}

/* Highest-priority cue for this tick; *follow / *delay get an optional sting. */
static sfx_t pick(uint32_t ev, const app_game_snapshot_t *s, sfx_t *follow, uint8_t *delay)
{
    const bool fainted_now = s->fainted && !s_sfx.fainted;
    const bool woke = s_sfx.power == APP_POWER_DOZE && s->power != APP_POWER_DOZE;
    const uint32_t step_ticks = s->steps_total / STEP_TICK_EVERY;
    *follow = SFX_NONE;
    *delay = 0;

    if (ev & APP_EVENT_REVIVED) return SFX_REVIVE;
    if (fainted_now) return SFX_FAINT;
    if (ev & APP_EVENT_GOAL) return SFX_GOAL;
    if (ev & APP_EVENT_FED) return SFX_FED;
    if (ev & APP_EVENT_SLOT_DONE) {
        *follow = slot_result(s->slots.payout);
        *delay = RESULT_DELAY_TICKS;
        return SFX_REEL;
    }
    if (ev & APP_EVENT_GRID_DONE) return grid_result(s->grid.result);
    if (ev & APP_EVENT_GRID_FLIP) {
        const int t = s->grid.last_flip;
        return t >= 0 && t < APP_GRID_TILES && s->grid.tiles[t] > 1 ? SFX_TILE_BONUS : SFX_TILE;
    }
    if (ev & APP_EVENT_CARD_REVEAL) {
        *follow = card_sting(s->cards.result);
        *delay = STING_DELAY_TICKS;
        return SFX_FLIP;
    }
    if (ev & APP_EVENT_CASH_OUT) return SFX_COINS;
    if (ev & APP_EVENT_BOUGHT) return SFX_BUY;
    if (ev & APP_EVENT_ATE) return SFX_EAT;
    if (ev & APP_EVENT_SLOT_STOP) return SFX_REEL;
    if (ev & APP_EVENT_SLOT_SPIN) return SFX_SPIN;
    if (ev & (APP_EVENT_CARD_DEAL | APP_EVENT_CARD_NEXT)) return SFX_DEAL;
    if (ev & APP_EVENT_PETTED) return SFX_PET;
    if (ev & (APP_EVENT_NO_SPARKS | APP_EVENT_BLOCKED)) return SFX_BUZZ;
    if (woke) return SFX_WAKE;
    if (ev & APP_EVENT_OVERLAY) {
        if (s->overlay == APP_OVERLAY_NONE) return SFX_CLOSE;
        return s_sfx.overlay == APP_OVERLAY_NONE ? SFX_OPEN : SFX_TAP;
    }
    if (ev & (APP_EVENT_SETTINGS | APP_EVENT_ARCADE | APP_EVENT_WOKE)) return SFX_TAP;
    if (ev & (APP_EVENT_SCREEN | APP_EVENT_STORE_PAGE)) return SFX_SWIPE;
    if (ev & APP_EVENT_SPARK) return SFX_SPARK;
    if ((ev & APP_EVENT_STEP) && step_ticks != s_sfx.step_ticks) return SFX_STEP;
    return SFX_NONE;
}

static void remember(const app_game_snapshot_t *s)
{
    s_sfx.power = s->power;
    s_sfx.overlay = s->overlay;
    s_sfx.fainted = s->fainted;
    s_sfx.step_ticks = s->steps_total / STEP_TICK_EVERY;
}

void app_sfx_update(uint32_t events, const app_game_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    if (!s_sfx.started) {
        /* Boot: open the codec once and baseline the state; the loaded save's
         * SETTINGS event would otherwise click, so chime instead. */
        s_sfx.started = true;
        raylib_lite_game_audio_init();
        s_sfx.audio_open = true;
        load_sounds();
        set_level(snapshot->sound);
        remember(snapshot);
        if (snapshot->power != APP_POWER_DOZE && s_sfx.level != APP_SOUND_OFF) {
            play(SFX_WAKE);
        }
        return;
    }
    load_sounds();   /* No-op once tried; retries if the codec was late. */
    if (snapshot->sound != s_sfx.level) {
        set_level(snapshot->sound);
    }
    sfx_t follow;
    uint8_t delay;
    const sfx_t now = pick(events, snapshot, &follow, &delay);
    remember(snapshot);
    /* Screen off (or sound off): nothing at all, and no stale sting later. */
    if (s_sfx.level == APP_SOUND_OFF || snapshot->power == APP_POWER_DOZE) {
        s_sfx.pending = SFX_NONE;
        return;
    }
    play(now);
    if (follow != SFX_NONE) {
        s_sfx.pending = follow;
        s_sfx.pending_ticks = delay;
    } else if (s_sfx.pending != SFX_NONE) {
        if (s_sfx.pending_ticks) {
            --s_sfx.pending_ticks;
        } else {
            play(s_sfx.pending);
            s_sfx.pending = SFX_NONE;
        }
    }
}

void app_sfx_close(void)
{
    if (!s_sfx.audio_open) {
        return;
    }
    for (int i = SFX_NONE + 1; i < SFX_COUNT; ++i) {
        if (s_sfx.sounds[i].frameCount) {
            raylib_lite_game_audio_stop_sound(s_sfx.sounds[i]);
            raylib_lite_game_audio_unload_sound(s_sfx.sounds[i]);
        }
    }
    raylib_lite_game_audio_close();
    memset(&s_sfx, 0, sizeof(s_sfx));
}
