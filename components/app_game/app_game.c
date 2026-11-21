// SPDX-License-Identifier: Apache-2.0
/* Pet model: steps -> sparks, stats, mood, screen navigation and touch rules.
 * Pure C; drawing and hardware live in app_game_runtime. */
#include <stdlib.h>
#include <string.h>
#include "include/app_game.h"
#include "include/app_game_layout.h"
#include "private/booth.h"
#include "private/cards.h"
#include "private/pedometer.h"
#include "private/bits.h"
#include "private/grid.h"
#include "private/slots.h"

#define TICKS_PER_SECOND 30U
enum {
    STEPS_PER_SPARK = APP_STEPS_PER_SPARK,
    START_SPARKS = 30,
    START_SNACK = 60,            /* A new pet is not hungry on its first morning. */
    START_HAPPINESS = 70,
    STAT_MAX = 100,
    FRIENDSHIP_MAX = 999,
    LOW_STAT = 25,
    GOAL_FRIENDSHIP = 50,
    FEED_FRIENDSHIP = 2,
    PET_HAPPINESS = 3,
    MAX_SENSOR_DELTA = 100000,   /* Larger jumps are treated as a sensor reset. */
};
enum {
    SNACK_DECAY_TICKS = 120 * TICKS_PER_SECOND,
    HAPPY_DECAY_TICKS = 180 * TICKS_PER_SECOND,
    SLEEP_AFTER_TICKS = 90 * TICKS_PER_SECOND,
    WALK_MOOD_TICKS = 3 * TICKS_PER_SECOND,
    PET_COOLDOWN_TICKS = 10,
    REACT_PET_TICKS = 40,
    REACT_EAT_TICKS = 66,
    REACT_GRUMPY_TICKS = 34,
    REACT_WIN_TICKS = 44,
    REACT_LOSE_TICKS = 60,
    REACT_SURPRISE_TICKS = 26,
    REACT_GOAL_TICKS = 90,
    SLOTS_BIG_WIN = 30,          /* Sparks; bigger wins get the long celebration. */
    STUCK_TOUCH_TICKS = 10 * TICKS_PER_SECOND,
};

static const uint16_t s_sleep_s[APP_SLEEP_OPTIONS] = {15, 30, 60, 120};
static const uint8_t s_bright_pct[APP_BRIGHT_OPTIONS] = {100, 80, 60};
enum { DEFAULT_SLEEP_IDX = 1, DEFAULT_BRIGHT_IDX = 0, SETTINGS_PAGES = 2, RESET_ROW = 6 };
/* Volume order when tapping SOUND: off, low, mid, high. */
static const app_sound_t s_sound_cycle[APP_SOUND_COUNT] = {
    APP_SOUND_OFF, APP_SOUND_LOW, APP_SOUND_MID, APP_SOUND_HIGH,
};

static const app_food_t s_foods[APP_FOOD_COUNT] = {
    {"CELL", 10, 15, 2},
    {"CHIP", 25, 35, 6},
    {"MEGA", 60, 80, 20},
};

/* Prices in sparks (20 steps each): the priciest item is ~6000 steps. */
static const app_store_item_t s_store[] = {
    {APP_ITEM_COLOR, 1, 40, "ORANGE", "MOSAICO ORANGE EYES"},
    {APP_ITEM_COLOR, 2, 60, "CYAN", "COOL CYAN EYES"},
    {APP_ITEM_COLOR, 3, 80, "PINK", "HOT PINK EYES"},
    {APP_ITEM_ANIM, APP_ANIM_TRAIL, 100, "SPARK TRAIL", "SPARKS FALL AS YOU WALK"},
    {APP_ITEM_COLOR, 4, 100, "WHITE", "CLEAN WHITE EYES"},
    {APP_ITEM_COLOR, 5, 120, "AMBER", "WARM AMBER EYES"},
    {APP_ITEM_STYLE, APP_PIXEL_FINE, 80, "FINE PIXELS", "SMALLER SHARPER PIXELS"},
    {APP_ITEM_ANIM, APP_ANIM_DANCE, 150, "STEP DANCE", "DANCES EVERY 100 STEPS"},
    {APP_ITEM_STYLE, APP_PIXEL_SMOOTH, 150, "SMOOTH", "NO PIXELS AT ALL"},
    {APP_ITEM_ANIM, APP_ANIM_STARS, 200, "STAR EYES", "STARS WHEN HAPPY"},
    {APP_ITEM_GAME, APP_GAME_SLOTS, 100, "SPARK SLOTS", "STOP THE REELS TO WIN"},
    {APP_ITEM_GAME, APP_GAME_GRID, 200, "SPARK GRID", "FLIP TILES DODGE DUDS"},
    {APP_ITEM_GAME, APP_GAME_BITS, 250, "BIT FLIP", "MATCH NUMBERS IN BINARY"},
    {APP_ITEM_ANIM, APP_ANIM_RAINBOW, 300, "RAINBOW", "COLORS WHILE WALKING"},
    {APP_ITEM_GAME, APP_GAME_PHOTO, 300, "PHOTO BOOTH", "LO-RES PICS AND GIFS"},
    {APP_ITEM_GAME, APP_GAME_CARDS, 400, "HI-LO", "DOUBLE OR NOTHING CARDS"},
    {APP_ITEM_FILTER, APP_FILTER_DITHER, 50, "DITHER", "RETRO DOT SHADING"},
    {APP_ITEM_FILTER, APP_FILTER_SCANLINES, 60, "SCANLINES", "OLD TV LINES"},
    {APP_ITEM_LENS, APP_PHOTO_RES_MID, 80, "MID RES", "SHARPER 64X64 PHOTOS"},
    {APP_ITEM_FILTER, APP_FILTER_GREEN, 80, "GAME-GREEN", "FOUR POCKET GREENS"},
    {APP_ITEM_FILTER, APP_FILTER_HALFTONE, 100, "HALFTONE", "NEWSPAPER DOTS"},
    {APP_ITEM_FILTER, APP_FILTER_THERMAL, 120, "THERMAL", "HEAT CAMERA LOOK"},
    {APP_ITEM_FILTER, APP_FILTER_ASCII, 120, "ASCII", "PICTURES MADE OF TEXT"},
    {APP_ITEM_FILTER, APP_FILTER_STICKERS, 150, "EYE STICKERS", "YOUR PAL PHOTOBOMBS"},
    {APP_ITEM_LENS, APP_PHOTO_RES_HIGH, 180, "HI RES", "CRISP 128X128 PHOTOS"},
    {APP_ITEM_FILM, 0, 200, "COLOR FILM", "REAL 16-COLOR PHOTOS"},
};
#define STORE_COUNT ((int)(sizeof(s_store) / sizeof(s_store[0])))
#define STORE_PAGES ((STORE_COUNT + APP_STORE_PER_PAGE - 1) / APP_STORE_PER_PAGE)
#define FREE_OWNED (APP_OWN_COLOR_BIT(0) | APP_OWN_STYLE_BIT(APP_PIXEL_CHUNKY))
#define FREE_FILTERS APP_OWN_FILTER_BIT(APP_FILTER_NONE)
#define FREE_LENSES APP_OWN_LENS_BIT(APP_PHOTO_RES_LOW)
#define DANCE_EVERY_STEPS 100U
_Static_assert(sizeof(app_game_record_t) == 76, "bump SAVE_VERSION and migrate when the record changes");

struct app_game {
    app_game_config_t config;
    uint32_t tick;
    bool paused;
    int32_t pointer_x, pointer_y;
    bool pointer_down;
    int32_t press_x, press_y;
    app_screen_t screen;
    app_overlay_t overlay;
    app_arcade_t arcade;
    uint8_t store_page;
    uint8_t settings_page;
    uint8_t sleep_idx, bright_idx;     /* Indexes into s_sleep_s / s_bright_pct. */
    bool reset_armed;                  /* Finger went down on RESET PET. */
    uint16_t reset_hold_ticks;
    uint32_t owned;
    uint8_t filters_owned;
    uint8_t lenses_owned;
    uint8_t anims_on;
    app_sound_t sound;
    uint32_t step_goal;
    app_power_t power;
    uint32_t screen_idle_ticks;   /* Touch-free ticks; steps do not reset it. */
    bool swallow_touch;           /* The touch that woke the screen does nothing else. */
    uint32_t pointer_still_ticks; /* Ticks since the last pointer sample while pressed. */
    int8_t battery_pct;
    bool battery_charging;
    uint32_t events;
    uint32_t save_seq;

    uint32_t steps_total, steps_today, sparks;
    int32_t day;
    uint16_t spark_progress, friendship;
    uint8_t snack, happiness;     /* snack = temporary fullness from food */
    uint8_t hp;
    bool fainted, fed_today;
    uint16_t fed_streak;
    uint32_t history[APP_HISTORY_DAYS];
    app_net_state_t net;
    uint32_t ipv4;
    char setup_ap[APP_SETUP_AP_MAX];
    bool goal_awarded;
    app_pixel_style_t pixel_style;
    uint8_t color_index;
    int8_t last_food;

    uint32_t idle_ticks, walk_ticks, snack_timer, happy_timer;
    app_mood_t reaction;
    uint16_t reaction_ticks, pet_cooldown;
    app_mood_t mood;

    pedometer_t pedometer;
    bool has_sensor, sensor_valid;
    uint32_t sensor_last;
    cards_t cards;
    slots_t slots;
    grid_t grid;
    bits_t bits;
    uint8_t arcade_row;
    booth_t booth;
    bool camera_present;
    uint16_t camera_gone_ticks;   /* On the booth page without a camera. */
};

static uint8_t add_stat(uint8_t value, int delta)
{
    int v = (int)value + delta;
    return (uint8_t)(v < 0 ? 0 : (v > STAT_MAX ? STAT_MAX : v));
}

static void add_friendship(app_game_handle_t g, int delta)
{
    int v = (int)g->friendship + delta;
    g->friendship = (uint16_t)(v < 0 ? 0 : (v > FRIENDSHIP_MAX ? FRIENDSHIP_MAX : v));
}

static void react(app_game_handle_t g, app_mood_t mood, uint16_t ticks)
{
    g->reaction = mood;
    g->reaction_ticks = ticks;
}

static void changed(app_game_handle_t g)
{
    ++g->save_seq;
}

/* Walking is the pet's food: half the daily goal feeds it for the day. */
static uint32_t food_need(const struct app_game *g)
{
    const uint32_t need = g->step_goal / APP_FOOD_GOAL_DIV;
    return need ? need : 1;
}

/* Fullness = today's walking toward food_need, topped up by snacks. */
static uint8_t fullness(const struct app_game *g)
{
    const uint32_t walked = g->steps_today >= food_need(g) ? STAT_MAX
                            : g->steps_today * STAT_MAX / food_need(g);
    const uint32_t total = walked + g->snack;
    return (uint8_t)(total > STAT_MAX ? STAT_MAX : total);
}

/* Called whenever steps_today grows: celebrate the day's meal, revive. */
static void check_fed(app_game_handle_t g)
{
    if (g->fed_today || g->steps_today < food_need(g)) {
        return;
    }
    g->fed_today = true;
    g->events |= APP_EVENT_FED;
    if (g->fainted) {
        g->fainted = false;
        g->hp = 1;
        g->events |= APP_EVENT_REVIVED;
    }
    react(g, APP_MOOD_HAPPY, REACT_WIN_TICKS);
}

static void credit_steps(app_game_handle_t g, uint32_t steps)
{
    if (!steps) {
        return;
    }
    if ((g->anims_on & (1U << APP_ANIM_DANCE))
            && (g->steps_total + steps) / DANCE_EVERY_STEPS > g->steps_total / DANCE_EVERY_STEPS) {
        g->events |= APP_EVENT_DANCE;
    }
    g->steps_total += steps;
    g->steps_today += steps;
    uint32_t progress = g->spark_progress + steps;
    if (progress >= STEPS_PER_SPARK) {
        g->sparks += progress / STEPS_PER_SPARK;
        progress %= STEPS_PER_SPARK;
        g->events |= APP_EVENT_SPARK;
    }
    g->spark_progress = (uint16_t)progress;
    g->walk_ticks = 0;
    g->idle_ticks = 0;
    g->events |= APP_EVENT_STEP;
    check_fed(g);
    if (!g->goal_awarded && g->steps_today >= g->step_goal) {
        g->goal_awarded = true;
        add_friendship(g, GOAL_FRIENDSHIP);
        g->happiness = STAT_MAX;
        react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS);
        g->events |= APP_EVENT_GOAL;
    }
    changed(g);
}

static void fresh_pet(app_game_handle_t g)
{
    g->screen = APP_SCREEN_HOME;
    g->overlay = APP_OVERLAY_NONE;
    g->arcade = APP_ARCADE_LOBBY;
    g->store_page = 0;
    g->owned = FREE_OWNED;
    g->filters_owned = FREE_FILTERS;
    g->lenses_owned = FREE_LENSES;
    g->anims_on = 0;
    g->sound = APP_SOUND_MID;
    g->settings_page = 0;
    g->sleep_idx = DEFAULT_SLEEP_IDX;
    g->bright_idx = DEFAULT_BRIGHT_IDX;
    g->reset_armed = false;
    g->reset_hold_ticks = 0;
    g->step_goal = APP_GOAL_DEFAULT;
    g->power = APP_POWER_AWAKE;
    g->screen_idle_ticks = 0;
    g->swallow_touch = false;
    g->steps_total = g->steps_today = 0;
    g->day = 0;
    g->sparks = START_SPARKS;
    g->spark_progress = 0;
    g->friendship = 0;
    g->snack = START_SNACK;
    g->hp = APP_HP_MAX;
    g->fainted = false;
    g->fed_today = false;
    g->fed_streak = 0;
    memset(g->history, 0, sizeof(g->history));
    g->happiness = START_HAPPINESS;
    g->goal_awarded = false;
    g->pixel_style = APP_PIXEL_CHUNKY;
    g->color_index = 0;
    g->last_food = -1;
    g->idle_ticks = 0;
    g->walk_ticks = WALK_MOOD_TICKS;
    g->snack_timer = g->happy_timer = 0;
    g->reaction_ticks = g->pet_cooldown = 0;
    g->mood = APP_MOOD_IDLE;
    g->cards.view.best_streak = 0;
    cards_init(&g->cards, g->cards.rng);
    slots_init(&g->slots, g->slots.rng);
    grid_init(&g->grid, g->grid.rng);
    g->bits.view.best = 0;
    bits_init(&g->bits, g->bits.rng);
    g->arcade_row = 0;
    /* Keep what the runtime reported about the camera and stored photos. */
    const app_booth_snapshot_t status = g->booth.view;
    booth_init(&g->booth);
    booth_set_status(&g->booth, status.camera, status.photos, status.gifs);
    pedometer_reset(&g->pedometer);
}

app_game_err_t app_game_create(const app_game_config_t *config, app_game_handle_t *ret_handle)
{
    if (!ret_handle) {
        return APP_GAME_INVALID_ARGUMENT;
    }
    *ret_handle = NULL;
    if (!config || !config->width || !config->height) {
        return APP_GAME_INVALID_ARGUMENT;
    }
    app_game_handle_t g = calloc(1, sizeof(*g));
    if (!g) {
        return APP_GAME_NO_MEMORY;
    }
    g->config = *config;
    g->battery_pct = -1;
    cards_init(&g->cards, config->seed);
    slots_init(&g->slots, config->seed ^ 0xa5a5a5a5U);
    grid_init(&g->grid, config->seed ^ 0x5a5a5a5aU);
    bits_init(&g->bits, config->seed ^ 0x3c3c3c3cU);
    fresh_pet(g);
    *ret_handle = g;
    return APP_GAME_OK;
}

void app_game_destroy(app_game_handle_t *handle)
{
    if (handle) {
        free(*handle);
        *handle = NULL;
    }
}

void app_game_reset(app_game_handle_t g)
{
    if (g) {
        fresh_pet(g);
        g->events |= APP_EVENT_SETTINGS | APP_EVENT_SCREEN;
        changed(g);
    }
}

/* RESET PET from Settings: a brand-new pet, but the settings stay. */
static void reset_pet_keep_settings(app_game_handle_t g)
{
    const app_sound_t sound = g->sound;
    const uint8_t sleep_idx = g->sleep_idx, bright_idx = g->bright_idx;
    const uint32_t goal = g->step_goal;
    fresh_pet(g);
    g->sound = sound;
    g->sleep_idx = sleep_idx;
    g->bright_idx = bright_idx;
    g->step_goal = goal;
    g->swallow_touch = true;   /* The finger still on the glass does nothing else. */
    g->events |= APP_EVENT_SETTINGS | APP_EVENT_SCREEN | APP_EVENT_OVERLAY;
    changed(g);
}

void app_game_set_paused(app_game_handle_t g, bool paused)
{
    if (g) {
        g->paused = paused;
    }
}

/* Games live inside the arcade; the photo booth is a page of its own that
 * exists only while a camera module is plugged in. */
static int last_screen(const struct app_game *g)
{
    return g->camera_present ? APP_SCREEN_BOOTH : APP_SCREEN_ARCADE;
}

static void set_screen(app_game_handle_t g, app_screen_t screen)
{
    if (g->screen == screen) {
        return;
    }
    g->screen = screen;
    g->events |= APP_EVENT_SCREEN;
    if (screen == APP_SCREEN_BOOTH) {
        booth_enter(&g->booth);
    }
}

static void go_screen(app_game_handle_t g, int delta)
{
    int next = (int)g->screen + delta;
    if (next < 0 || next > last_screen(g)) {
        return;
    }
    set_screen(g, (app_screen_t)next);
}

static void tap_home(app_game_handle_t g, int32_t x, int32_t y)
{
    if (!app_rect_hit(app_home_eyes_rect(), x, y) || g->pet_cooldown) {
        return;
    }
    g->pet_cooldown = PET_COOLDOWN_TICKS;
    g->happiness = add_stat(g->happiness, PET_HAPPINESS);
    add_friendship(g, 1);
    react(g, APP_MOOD_HAPPY, REACT_PET_TICKS);
    g->events |= APP_EVENT_PETTED;
    changed(g);
}

/* A fainted pet can't eat snacks or play: only walking revives it. */
static bool blocked_by_faint(app_game_handle_t g)
{
    if (g->fainted) {
        g->events |= APP_EVENT_BLOCKED;
    }
    return g->fainted;
}

static void tap_feed(app_game_handle_t g, int32_t x, int32_t y)
{
    if (blocked_by_faint(g)) {
        return;
    }
    for (int i = 0; i < APP_FOOD_COUNT; ++i) {
        if (!app_rect_hit(app_feed_item_rect(i), x, y)) {
            continue;
        }
        const app_food_t *food = &s_foods[i];
        if (g->sparks < food->cost) {
            react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
            g->events |= APP_EVENT_NO_SPARKS;
            return;
        }
        g->sparks -= food->cost;
        g->snack = add_stat(g->snack, food->hunger);
        g->happiness = add_stat(g->happiness, food->happiness);
        add_friendship(g, FEED_FRIENDSHIP);
        g->last_food = (int8_t)i;
        react(g, APP_MOOD_EATING, REACT_EAT_TICKS);
        g->events |= APP_EVENT_ATE;
        changed(g);
        return;
    }
}

static void tap_cards(app_game_handle_t g, int32_t x, int32_t y)
{
    if (blocked_by_faint(g)) {
        return;
    }
    const app_cards_phase_t phase = g->cards.view.phase;
    if (phase == APP_CARDS_READY && app_rect_hit(app_cards_deal_rect(), x, y)) {
        const uint32_t cost = cards_deal(&g->cards, g->sparks);
        if (!cost) {
            react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
            g->events |= APP_EVENT_NO_SPARKS;
            return;
        }
        g->sparks -= cost;
        g->events |= APP_EVENT_CARD_DEAL;
        changed(g);
        return;
    }
    if (phase != APP_CARDS_GUESS) {
        return;
    }
    if (app_rect_hit(app_cards_cash_rect(), x, y)) {
        const uint32_t won = cards_cash_out(&g->cards);
        if (won) {
            g->sparks += won;
            react(g, APP_MOOD_HAPPY, REACT_WIN_TICKS);
            g->events |= APP_EVENT_CASH_OUT;
            changed(g);
        }
        return;
    }
    bool guessed = false;
    if (app_rect_hit(app_cards_hi_rect(), x, y)) {
        guessed = cards_guess(&g->cards, CARDS_GUESS_HIGHER);
    } else if (app_rect_hit(app_cards_lo_rect(), x, y)) {
        guessed = cards_guess(&g->cards, CARDS_GUESS_LOWER);
    }
    if (!guessed) {
        return;
    }
    g->events |= APP_EVENT_CARD_REVEAL;
    switch (g->cards.view.result) {
    case APP_CARD_RESULT_WIN: react(g, APP_MOOD_HAPPY, REACT_WIN_TICKS); break;
    case APP_CARD_RESULT_LOSE: react(g, APP_MOOD_SAD, REACT_LOSE_TICKS); break;
    default: react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS); break;
    }
    changed(g);
}

static void set_overlay(app_game_handle_t g, app_overlay_t overlay)
{
    if (g->overlay != overlay) {
        g->overlay = overlay;
        g->events |= APP_EVENT_OVERLAY;
    }
}

static void set_arcade(app_game_handle_t g, app_arcade_t arcade)
{
    if (g->arcade != arcade) {
        g->arcade = arcade;
        g->events |= APP_EVENT_ARCADE;
    }
}

static void set_power(app_game_handle_t g, app_power_t power)
{
    if (g->power != power) {
        g->power = power;
        g->events |= APP_EVENT_POWER;
    }
}

/* Screen off: close panels and go home, so waking shows the pet. */
static void doze(app_game_handle_t g)
{
    set_overlay(g, APP_OVERLAY_NONE);
    set_screen(g, APP_SCREEN_HOME);
    set_power(g, APP_POWER_DOZE);
}

static void wake(app_game_handle_t g)
{
    g->screen_idle_ticks = 0;
    if (g->power == APP_POWER_AWAKE) {
        return;
    }
    set_power(g, APP_POWER_AWAKE);
    react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
    g->events |= APP_EVENT_WOKE;
}

static uint32_t doze_after_ticks(const struct app_game *g)
{
    const bool low = g->battery_pct >= 0 && g->battery_pct <= APP_LOW_BATTERY_PCT && !g->battery_charging;
    const uint32_t chosen = s_sleep_s[g->sleep_idx < APP_SLEEP_OPTIONS ? g->sleep_idx : DEFAULT_SLEEP_IDX];
    return (low && APP_LOW_BATTERY_DOZE_S < chosen ? APP_LOW_BATTERY_DOZE_S : chosen) * TICKS_PER_SECOND;
}

static void update_power(app_game_handle_t g)
{
    if (g->power == APP_POWER_DOZE) {
        return;
    }
    /* A held finger keeps the screen on, but a press with no update for
     * STUCK_TOUCH_TICKS is treated as a lost release so dozing still happens. */
    if (g->pointer_down && ++g->pointer_still_ticks < STUCK_TOUCH_TICKS) {
        return;
    }
    /* The hotspot name must stay readable while someone types on their phone
     * (the hotspot itself times out in the runtime). */
    if (g->overlay == APP_OVERLAY_WIFI && g->net == APP_NET_SETUP) {
        g->screen_idle_ticks = 0;
        set_power(g, APP_POWER_AWAKE);
        return;
    }
    ++g->screen_idle_ticks;
    const uint32_t doze_at = doze_after_ticks(g);
    const uint32_t dim_at = doze_at * 2U / 3U;   /* Dim for the last third. */
    if (g->screen_idle_ticks >= doze_at) {
        doze(g);
    } else if (g->screen_idle_ticks >= dim_at) {
        set_power(g, APP_POWER_DIM);
    }
}

static uint32_t item_bit(const app_store_item_t *item)
{
    switch (item->kind) {
    case APP_ITEM_COLOR: return APP_OWN_COLOR_BIT(item->value);
    case APP_ITEM_STYLE: return APP_OWN_STYLE_BIT(item->value);
    case APP_ITEM_GAME: return APP_OWN_GAME_BIT(item->value);
    case APP_ITEM_FILTER: return APP_OWN_FILTER_BIT(item->value);
    case APP_ITEM_LENS: return APP_OWN_LENS_BIT(item->value);
    case APP_ITEM_FILM: return APP_OWN_FILM_BIT;
    default: return APP_OWN_ANIM_BIT(item->value);
    }
}

/* Looks and lenses live in their own bytes; everything else in `owned`. */
static bool owns_item(const struct app_game *g, const app_store_item_t *item)
{
    switch (item->kind) {
    case APP_ITEM_FILTER: return (g->filters_owned & item_bit(item)) != 0;
    case APP_ITEM_LENS:
    case APP_ITEM_FILM: return (g->lenses_owned & item_bit(item)) != 0;
    default: return (g->owned & item_bit(item)) != 0;
    }
}

static void grant_item(app_game_handle_t g, const app_store_item_t *item)
{
    switch (item->kind) {
    case APP_ITEM_FILTER: g->filters_owned |= (uint8_t)item_bit(item); break;
    case APP_ITEM_LENS:
    case APP_ITEM_FILM: g->lenses_owned |= (uint8_t)item_bit(item); break;
    default: g->owned |= item_bit(item); break;
    }
}

static bool booth_item(const app_store_item_t *item)
{
    return item->kind == APP_ITEM_FILTER || item->kind == APP_ITEM_LENS || item->kind == APP_ITEM_FILM
           || (item->kind == APP_ITEM_GAME && item->value == APP_GAME_PHOTO);
}

static bool item_visible(const app_store_item_t *item, bool camera)
{
    return camera || !booth_item(item);
}

static int store_pages(const struct app_game *g)
{
    const int count = app_game_store_visible_count(g->camera_present);
    return count ? (count + APP_STORE_PER_PAGE - 1) / APP_STORE_PER_PAGE : 1;
}

static void open_game(app_game_handle_t g, app_minigame_t game)
{
    set_overlay(g, APP_OVERLAY_NONE);
    if (game == APP_GAME_PHOTO) {
        if (g->camera_present) {
            set_screen(g, APP_SCREEN_BOOTH);
        }
        return;
    }
    set_screen(g, APP_SCREEN_ARCADE);
    const app_arcade_t cabinet = game == APP_GAME_GRID ? APP_ARCADE_GRID
                                 : (game == APP_GAME_BITS ? APP_ARCADE_BITS : APP_ARCADE_SLOTS);
    g->arcade_row = (uint8_t)((cabinet - 1) / APP_ARCADE_PER_ROW);
    set_arcade(g, cabinet);
}

static void open_store_at(app_game_handle_t g, app_item_kind_t kind, uint8_t value)
{
    const int index = app_game_store_find(kind, value);
    for (int pos = 0; index >= 0 && pos < app_game_store_visible_count(g->camera_present); ++pos) {
        if (app_game_store_visible(g->camera_present, pos) == index) {
            g->store_page = (uint8_t)(pos / APP_STORE_PER_PAGE);
            break;
        }
    }
    set_overlay(g, APP_OVERLAY_STORE);
}

/** Select an owned color/style, or toggle an owned animation. */
static void equip(app_game_handle_t g, const app_store_item_t *item, bool just_bought)
{
    switch (item->kind) {
    case APP_ITEM_COLOR: g->color_index = item->value; break;
    case APP_ITEM_STYLE: g->pixel_style = (app_pixel_style_t)item->value; break;
    case APP_ITEM_GAME: open_game(g, (app_minigame_t)item->value); break;
    case APP_ITEM_FILTER: booth_select_filter(&g->booth, item->value); break;
    case APP_ITEM_LENS: booth_select_lens(&g->booth, item->value); break;
    case APP_ITEM_FILM: booth_select_color(&g->booth, true); break;
    default:
        if (just_bought) {
            g->anims_on |= (uint8_t)(1U << item->value);
        } else {
            g->anims_on ^= (uint8_t)(1U << item->value);
        }
        break;
    }
    g->events |= APP_EVENT_SETTINGS;
    changed(g);
}

static void tap_style(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_panel_back_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_MENU);
        return;
    }
    for (int i = 0; i < APP_PIXEL_COUNT; ++i) {
        if (!app_rect_hit(app_style_pixel_rect(i), x, y)) {
            continue;
        }
        if (!(g->owned & APP_OWN_STYLE_BIT(i))) {
            open_store_at(g, APP_ITEM_STYLE, (uint8_t)i);
            return;
        }
        g->pixel_style = (app_pixel_style_t)i;
        g->events |= APP_EVENT_SETTINGS;
        react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
        changed(g);
        return;
    }
    for (int i = 0; i < APP_COLOR_COUNT; ++i) {
        if (!app_rect_hit(app_style_color_rect(i), x, y)) {
            continue;
        }
        if (!(g->owned & APP_OWN_COLOR_BIT(i))) {
            open_store_at(g, APP_ITEM_COLOR, (uint8_t)i);
            return;
        }
        g->color_index = (uint8_t)i;
        g->events |= APP_EVENT_SETTINGS;
        react(g, APP_MOOD_HAPPY, REACT_SURPRISE_TICKS);
        changed(g);
        return;
    }
}

static void tap_store(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_panel_back_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_MENU);
        return;
    }
    for (int row = 0; row < APP_STORE_PER_PAGE; ++row) {
        const int index = app_game_store_visible(g->camera_present, g->store_page * APP_STORE_PER_PAGE + row);
        if (index < 0) {
            return;
        }
        if (!app_rect_hit(app_store_row_rect(row), x, y)) {
            continue;
        }
        const app_store_item_t *item = &s_store[index];
        if (owns_item(g, item)) {
            equip(g, item, false);
            return;
        }
        if (booth_item(item) && item->kind != APP_ITEM_GAME && !(g->owned & APP_OWN_GAME_BIT(APP_GAME_PHOTO))) {
            /* Looks and lenses need the booth: show it instead of selling a dead item. */
            open_store_at(g, APP_ITEM_GAME, APP_GAME_PHOTO);
            react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
            g->events |= APP_EVENT_STORE_PAGE;
            return;
        }
        if (g->sparks < item->cost) {
            react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
            g->events |= APP_EVENT_NO_SPARKS;
            return;
        }
        g->sparks -= item->cost;
        grant_item(g, item);
        equip(g, item, true);
        react(g, APP_MOOD_HAPPY, REACT_WIN_TICKS);
        g->events |= APP_EVENT_BOUGHT;
        return;
    }
}

/* Goal changes never re-award today's bonus: goal_awarded stays set until the
 * day rolls over, and a lowered goal pays out only on the next counted step. */
static void tap_goal(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_panel_back_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_SETTINGS);
        return;
    }
    uint32_t goal = g->step_goal;
    if (app_rect_hit(app_goal_minus_rect(), x, y) && goal > APP_GOAL_MIN) {
        goal -= APP_GOAL_STEP;
        react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
    } else if (app_rect_hit(app_goal_plus_rect(), x, y) && goal < APP_GOAL_MAX) {
        goal += APP_GOAL_STEP;
        react(g, APP_MOOD_HAPPY, REACT_SURPRISE_TICKS);
    } else {
        return;
    }
    g->step_goal = goal;
    g->events |= APP_EVENT_SETTINGS;
    changed(g);
}

static bool wifi_needs_setup(const struct app_game *g)
{
    return g->net == APP_NET_NO_CONFIG || g->net == APP_NET_FAILED;
}

/* The panel's single button: stop a running setup, otherwise (re)start it. */
static void tap_wifi(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_panel_back_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_SETTINGS);
    } else if (app_rect_hit(app_wifi_action_rect(), x, y) && g->net != APP_NET_OFF) {
        g->events |= g->net == APP_NET_SETUP ? APP_EVENT_WIFI_STOP : APP_EVENT_WIFI_SETUP;
    }
}

static void open_wifi(app_game_handle_t g)
{
    if (g->net == APP_NET_OFF) {
        return;
    }
    /* Nothing to join yet: opening it starts the setup hotspot right away. */
    if (wifi_needs_setup(g)) {
        g->events |= APP_EVENT_WIFI_SETUP;
    }
    set_overlay(g, APP_OVERLAY_WIFI);
}

/* Rows: 0 SOUND, 1 DAILY GOAL, 2 WI-FI, 3 SCREEN SLEEP, 4 BRIGHTNESS, 5 ABOUT,
 * 6 RESET PET (held, see update_reset). Taps cycle a value or open a panel. */
static void tap_settings(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_panel_back_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_MENU);
        return;
    }
    for (int row = 0; row < APP_SETTINGS_PER_PAGE; ++row) {
        const int index = g->settings_page * APP_SETTINGS_PER_PAGE + row;
        if (index >= APP_SETTINGS_ROWS || !app_rect_hit(app_settings_row_rect(row), x, y)) {
            continue;
        }
        switch (index) {
        case 0: {
            int at = 0;
            while (at < APP_SOUND_COUNT && s_sound_cycle[at] != g->sound) {
                ++at;
            }
            g->sound = s_sound_cycle[(at + 1) % APP_SOUND_COUNT];
            break;
        }
        case 1: set_overlay(g, APP_OVERLAY_GOAL); return;
        case 2: open_wifi(g); return;
        case 3: g->sleep_idx = (uint8_t)((g->sleep_idx + 1) % APP_SLEEP_OPTIONS); break;
        case 4: g->bright_idx = (uint8_t)((g->bright_idx + 1) % APP_BRIGHT_OPTIONS); break;
        default: return;   /* ABOUT is read-only; RESET needs a hold. */
        }
        g->events |= APP_EVENT_SETTINGS;
        changed(g);
        return;
    }
}

static bool on_reset_row(const struct app_game *g, int32_t x, int32_t y)
{
    return g->overlay == APP_OVERLAY_SETTINGS
           && g->settings_page == RESET_ROW / APP_SETTINGS_PER_PAGE
           && app_rect_hit(app_settings_row_rect(RESET_ROW % APP_SETTINGS_PER_PAGE), x, y);
}

static void update_reset(app_game_handle_t g)
{
    if (!g->reset_armed) {
        return;
    }
    if (!g->pointer_down || !on_reset_row(g, g->pointer_x, g->pointer_y)) {
        g->reset_armed = false;   /* Let go or slid off: cancelled. */
        g->reset_hold_ticks = 0;
        return;
    }
    if (++g->reset_hold_ticks >= APP_RESET_HOLD_S * TICKS_PER_SECOND) {
        g->reset_armed = false;
        g->reset_hold_ticks = 0;
        reset_pet_keep_settings(g);
    }
}

static void tap_menu(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_menu_wifi_rect(), x, y) && g->net != APP_NET_OFF) {
        open_wifi(g);
    } else if (app_rect_hit(app_menu_sleep_rect(), x, y)) {
        doze(g);
    } else if (app_rect_hit(app_menu_store_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_STORE);
    } else if (app_rect_hit(app_menu_style_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_STYLE);
    } else if (app_rect_hit(app_menu_settings_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_SETTINGS);
    } else if (y >= APP_MENU_H) {
        set_overlay(g, APP_OVERLAY_NONE);
    }
}

static void tap_slots(app_game_handle_t g, int32_t x, int32_t y)
{
    if (g->slots.view.phase != APP_SLOTS_SPINNING && blocked_by_faint(g)) {
        return;
    }
    const bool on_button = app_rect_hit(app_slots_button_rect(), x, y);
    if (g->slots.view.phase == APP_SLOTS_SPINNING) {
        /* While spinning, the reels and the button both stop the next reel. */
        if (on_button || app_rect_hit(app_slots_reels_rect(), x, y)) {
            slots_stop(&g->slots);
        }
        return;
    }
    if (!on_button) {
        return;
    }
    const uint32_t bet = slots_spin(&g->slots, g->sparks);
    if (!bet) {
        react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
        g->events |= APP_EVENT_NO_SPARKS;
        return;
    }
    g->sparks -= bet;
    g->events |= APP_EVENT_SLOT_SPIN;
    changed(g);
}

/* Lobby: a tap on a cabinet opens its game (or the store for a locked one).
 * In a game: EXIT returns to the cabinets, anything else goes to the game. */
static void tap_grid(app_game_handle_t g, int32_t x, int32_t y)
{
    if (blocked_by_faint(g)) {
        return;
    }
    const app_grid_snapshot_t *v = &g->grid.view;
    if (app_rect_hit(app_grid_button_rect(), x, y)) {
        if (v->phase == APP_GRID_PLAYING) {
            const uint32_t won = grid_cash(&g->grid, &g->events);
            if (won) {
                g->sparks += won;
                react(g, APP_MOOD_HAPPY, REACT_WIN_TICKS);
                changed(g);
            }
            return;
        }
        const uint32_t cost = grid_deal(&g->grid, g->sparks);
        if (!cost) {
            react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
            g->events |= APP_EVENT_NO_SPARKS;
            return;
        }
        g->sparks -= cost;
        g->events |= APP_EVENT_CARD_DEAL;   /* Same "dealt" cue as the card game. */
        changed(g);
        return;
    }
    for (int i = 0; i < APP_GRID_TILES; ++i) {
        if (!app_rect_hit(app_grid_tile_rect(i), x, y)) {
            continue;
        }
        const uint32_t before = g->events;
        const uint32_t won = grid_flip(&g->grid, i, &g->events);
        if (won) {
            g->sparks += won;
            react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS);
            changed(g);
        } else if ((g->events & ~before) & APP_EVENT_GRID_DONE) {
            react(g, APP_MOOD_SAD, REACT_LOSE_TICKS);   /* Hit a dud. */
        } else if (g->grid.view.tiles[i] > 1) {
            react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
        }
        return;
    }
}

static void tap_bits(app_game_handle_t g, int32_t x, int32_t y)
{
    if (blocked_by_faint(g)) {
        return;
    }
    if (g->bits.view.phase == APP_BITS_PLAYING) {
        for (int i = 0; i < APP_BITS; ++i) {
            if (app_rect_hit(app_bits_switch_rect(i), x, y)) {
                bits_toggle(&g->bits, i, &g->events);
                return;
            }
        }
        return;
    }
    if (g->bits.view.phase != APP_BITS_READY || !app_rect_hit(app_bits_button_rect(), x, y)) {
        return;
    }
    const uint32_t cost = bits_start(&g->bits, g->sparks);
    if (!cost) {
        react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
        g->events |= APP_EVENT_NO_SPARKS;
        return;
    }
    g->sparks -= cost;
    g->events |= APP_EVENT_CARD_DEAL;   /* Same "started" cue as the other games. */
    changed(g);
}

static app_minigame_t cabinet_game(app_arcade_t cabinet)
{
    switch (cabinet) {
    case APP_ARCADE_GRID: return APP_GAME_GRID;
    case APP_ARCADE_BITS: return APP_GAME_BITS;
    case APP_ARCADE_CARDS: return APP_GAME_CARDS;
    default: return APP_GAME_SLOTS;
    }
}

static bool cabinet_owned(const struct app_game *g, app_arcade_t game)
{
    return (g->owned & APP_OWN_GAME_BIT(cabinet_game(game))) != 0;
}

static bool booth_owned(const struct app_game *g)
{
    return (g->owned & APP_OWN_GAME_BIT(APP_GAME_PHOTO)) != 0;
}

static void tap_booth(app_game_handle_t g, int32_t x, int32_t y)
{
    if (!g->camera_present) {
        return;   /* "Plug the camera in" is all the page shows. */
    }
    if (!booth_owned(g)) {
        if (app_rect_hit(app_booth_unlock_rect(), x, y)) {
            open_store_at(g, APP_ITEM_GAME, APP_GAME_PHOTO);
        }
        return;
    }
    const uint32_t events = booth_tap(&g->booth, x, y, g->filters_owned, g->lenses_owned,
                                      (g->lenses_owned & APP_OWN_FILM_BIT) != 0, g->fainted);
    g->events |= events;
    const app_booth_snapshot_t *v = &g->booth.view;
    if (events & APP_EVENT_SETTINGS) {
        react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
        changed(g);   /* The selected filter is saved. */
    } else if (v->phase == APP_BOOTH_REVIEW && v->result == APP_BOOTH_RESULT_FULL) {
        react(g, APP_MOOD_SAD, REACT_LOSE_TICKS);
    } else if (v->phase == APP_BOOTH_COUNTDOWN) {
        react(g, APP_MOOD_HAPPY, REACT_PET_TICKS);
    }
}

/* Lobby: a tap on a cabinet opens its game (or the store for a locked one).
 * In a game: EXIT returns to the cabinets, anything else goes to the game. */
static void tap_arcade(app_game_handle_t g, int32_t x, int32_t y)
{
    if (g->arcade != APP_ARCADE_LOBBY) {
        if (app_rect_hit(app_arcade_exit_rect(), x, y)) {
            set_arcade(g, APP_ARCADE_LOBBY);
            return;
        }
        switch (g->arcade) {
        case APP_ARCADE_CARDS: tap_cards(g, x, y); break;
        case APP_ARCADE_SLOTS: tap_slots(g, x, y); break;
        case APP_ARCADE_GRID: tap_grid(g, x, y); break;
        case APP_ARCADE_BITS: tap_bits(g, x, y); break;
        default: break;
        }
        return;
    }
    for (int slot = 0; slot < APP_ARCADE_PER_ROW; ++slot) {
        const int index = g->arcade_row * APP_ARCADE_PER_ROW + slot;
        if (index >= APP_ARCADE_CABINETS || !app_rect_hit(app_arcade_cabinet_rect(slot), x, y)) {
            continue;
        }
        const app_arcade_t game = (app_arcade_t)(index + 1);
        if (!cabinet_owned(g, game)) {
            open_store_at(g, APP_ITEM_GAME, (uint8_t)cabinet_game(game));
        } else {
            set_arcade(g, game);
        }
        return;
    }
}

static void handle_tap(app_game_handle_t g, int32_t x, int32_t y)
{
    switch (g->overlay) {
    case APP_OVERLAY_MENU: tap_menu(g, x, y); return;
    case APP_OVERLAY_STYLE: tap_style(g, x, y); return;
    case APP_OVERLAY_STORE: tap_store(g, x, y); return;
    case APP_OVERLAY_GOAL: tap_goal(g, x, y); return;
    case APP_OVERLAY_WIFI: tap_wifi(g, x, y); return;
    case APP_OVERLAY_SETTINGS: tap_settings(g, x, y); return;
    default: break;
    }
    switch (g->screen) {
    case APP_SCREEN_HOME: tap_home(g, x, y); break;
    case APP_SCREEN_FEED: tap_feed(g, x, y); break;
    case APP_SCREEN_ARCADE: tap_arcade(g, x, y); break;
    case APP_SCREEN_BOOTH: tap_booth(g, x, y); break;
    default: break;
    }
}

static void handle_swipe(app_game_handle_t g, int32_t dx, int32_t dy)
{
    const int32_t adx = abs(dx), ady = abs(dy);
    if (ady >= APP_PULL_MIN_PX && ady > adx) {
        if (g->overlay == APP_OVERLAY_NONE && dy > 0 && g->press_y < APP_PULL_ZONE_H) {
            set_overlay(g, APP_OVERLAY_MENU);
        } else if (g->overlay != APP_OVERLAY_NONE && dy < 0) {
            set_overlay(g, APP_OVERLAY_NONE);
        } else if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_ARCADE
                   && g->arcade == APP_ARCADE_LOBBY) {
            /* Swipe up for the next row of cabinets, down to go back. */
            const int row = (int)g->arcade_row + (dy < 0 ? 1 : -1);
            if (row >= 0 && row < APP_ARCADE_ROWS) {
                g->arcade_row = (uint8_t)row;
                g->events |= APP_EVENT_STORE_PAGE;
            }
        }
        return;
    }
    if (adx < APP_SWIPE_MIN_PX || adx <= ady) {
        return;
    }
    if (g->overlay == APP_OVERLAY_STORE) {
        const int page = (int)g->store_page + (dx < 0 ? 1 : -1);
        if (page >= 0 && page < store_pages(g)) {
            g->store_page = (uint8_t)page;
            g->events |= APP_EVENT_STORE_PAGE;
        }
    } else if (g->overlay == APP_OVERLAY_SETTINGS) {
        const int page = (int)g->settings_page + (dx < 0 ? 1 : -1);
        if (page >= 0 && page < SETTINGS_PAGES) {
            g->settings_page = (uint8_t)page;
            g->events |= APP_EVENT_STORE_PAGE;
        }
    } else if (g->overlay == APP_OVERLAY_NONE) {
        go_screen(g, dx < 0 ? 1 : -1);
    }
}

void app_game_set_pointer(app_game_handle_t g, int32_t x, int32_t y, bool pressed)
{
    if (!g) {
        return;
    }
    x = x < 0 ? 0 : (x >= g->config.width ? g->config.width - 1 : x);
    y = y < 0 ? 0 : (y >= g->config.height ? g->config.height - 1 : y);
    const bool was_down = g->pointer_down;
    g->pointer_x = x;
    g->pointer_y = y;
    g->pointer_down = pressed;
    g->pointer_still_ticks = 0;
    if (g->paused) {
        return;
    }
    if (pressed && !was_down) {
        g->press_x = x;
        g->press_y = y;
        g->idle_ticks = 0;
        if (g->power == APP_POWER_AWAKE && on_reset_row(g, x, y)) {
            g->reset_armed = true;
            g->reset_hold_ticks = 0;
        }
        if (g->power != APP_POWER_AWAKE) {
            wake(g);
            g->swallow_touch = true;
            return;
        }
        g->screen_idle_ticks = 0;
        if (g->mood == APP_MOOD_SLEEPY) {
            react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
            g->events |= APP_EVENT_WOKE;
        }
        return;
    }
    if (pressed || !was_down) {
        return;
    }
    if (g->swallow_touch) {
        g->swallow_touch = false;
        return;
    }
    const int32_t dx = x - g->press_x;
    const int32_t dy = y - g->press_y;
    if (abs(dx) <= APP_TAP_SLOP_PX && abs(dy) <= APP_TAP_SLOP_PX) {
        handle_tap(g, g->press_x, g->press_y);
    } else {
        handle_swipe(g, dx, dy);
    }
}

void app_game_add_accel(app_game_handle_t g, float x_g, float y_g, float z_g)
{
    if (!g || g->has_sensor) {
        return;
    }
    credit_steps(g, pedometer_sample(&g->pedometer, x_g, y_g, z_g, g->tick));
}

void app_game_set_sensor_steps(app_game_handle_t g, uint32_t cumulative)
{
    if (!g) {
        return;
    }
    g->has_sensor = true;
    if (!g->sensor_valid || cumulative < g->sensor_last
            || cumulative - g->sensor_last > MAX_SENSOR_DELTA) {
        g->sensor_valid = true;
        g->sensor_last = cumulative;
        return;
    }
    const uint32_t delta = cumulative - g->sensor_last;
    g->sensor_last = cumulative;
    credit_steps(g, delta);
}

void app_game_add_steps(app_game_handle_t g, uint32_t steps)
{
    if (g) {
        credit_steps(g, steps);
    }
}

void app_game_set_network(app_game_handle_t g, app_net_state_t state, uint32_t ipv4)
{
    if (g) {
        g->net = state;
        g->ipv4 = state == APP_NET_ONLINE ? ipv4 : 0;
    }
}

void app_game_set_setup_ap(app_game_handle_t g, const char *name)
{
    if (g) {
        g->setup_ap[0] = '\0';
        if (name) {
            strncat(g->setup_ap, name, sizeof(g->setup_ap) - 1);
        }
    }
}

void app_game_set_battery(app_game_handle_t g, int percent, bool charging)
{
    if (!g) {
        return;
    }
    g->battery_pct = (int8_t)(percent < 0 ? -1 : (percent > 100 ? 100 : percent));
    g->battery_charging = charging && percent >= 0;
}

void app_game_set_camera(app_game_handle_t g, bool present)
{
    if (!g || g->camera_present == present) {
        return;
    }
    g->camera_present = present;
    const int pages = store_pages(g);
    if (g->store_page >= pages) {
        g->store_page = (uint8_t)(pages - 1);
        g->events |= APP_EVENT_STORE_PAGE;
    }
}

void app_game_set_booth(app_game_handle_t g, bool camera, uint8_t photos, uint8_t gifs)
{
    if (g) {
        booth_set_status(&g->booth, camera, photos, gifs);
    }
}

bool app_game_take_booth_request(app_game_handle_t g, app_booth_request_t *request)
{
    if (!g || !request || g->booth.request.kind == APP_BOOTH_REQ_NONE) {
        return false;
    }
    *request = g->booth.request;
    g->booth.request.kind = APP_BOOTH_REQ_NONE;
    return true;
}

void app_game_booth_result(app_game_handle_t g, app_booth_result_t result)
{
    if (!g) {
        return;
    }
    g->events |= booth_result(&g->booth, result);
    switch (result) {
    case APP_BOOTH_RESULT_SAVED: react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS); break;
    case APP_BOOTH_RESULT_FULL:
    case APP_BOOTH_RESULT_FAILED: react(g, APP_MOOD_SAD, REACT_LOSE_TICKS); break;
    default: break;
    }
}

void app_game_wake(app_game_handle_t g)
{
    if (g) {
        wake(g);
    }
}

void app_game_set_day(app_game_handle_t g, int32_t day)
{
    if (!g || day <= 0 || day == g->day) {
        return;
    }
    if (day < g->day) {
        /* The clock or timezone moved backwards (e.g. UTC first, local time
         * later): a correction, not a new day, so today's progress stays. */
        g->day = day;
        changed(g);
        return;
    }
    if (g->day > 0) {
        /* Settle the day that ended, then every day the pet went unwalked. */
        const int32_t missed = day - g->day - 1;
        int hp = g->hp + (g->fed_today ? 1 : -1) - (missed > APP_HP_MAX ? APP_HP_MAX : missed);
        hp = hp < 0 ? 0 : (hp > APP_HP_MAX ? APP_HP_MAX : hp);
        g->hp = (uint8_t)hp;
        /* Skipped days are unfed and more recent, so they break the streak. */
        g->fed_streak = (g->fed_today && missed == 0) ? (uint16_t)(g->fed_streak + 1) : 0;
        /* History: yesterday first, then a zero for each skipped day. */
        const int32_t shift = 1 + (missed >= APP_HISTORY_DAYS ? APP_HISTORY_DAYS : missed);
        for (int i = APP_HISTORY_DAYS - 1; i >= 0; --i) {
            g->history[i] = i >= shift ? g->history[i - shift] : 0;
        }
        if (shift - 1 < APP_HISTORY_DAYS) {
            g->history[shift - 1] = g->steps_today;
        }
        if (g->hp == 0 && !g->fainted) {
            g->fainted = true;
            set_overlay(g, APP_OVERLAY_NONE);
        }
        g->events |= APP_EVENT_DAY;
    }
    if (g->day > 0) {
        g->steps_today = 0;
        g->goal_awarded = false;
        g->fed_today = false;
    }
    g->day = day;
    changed(g);
}

static bool booth_open(const struct app_game *g)
{
    return g->screen == APP_SCREEN_BOOTH && g->camera_present && booth_owned(g)
           && g->overlay == APP_OVERLAY_NONE && g->power != APP_POWER_DOZE;
}

static bool booth_capturing(const struct app_game *g)
{
    const app_booth_phase_t p = g->booth.view.phase;
    return p == APP_BOOTH_COUNTDOWN || p == APP_BOOTH_FLASH || p == APP_BOOTH_RECORD;
}

static void update_booth(app_game_handle_t g)
{
    /* Unplugged on the booth page: say so, then go Home if it stays gone. */
    const bool gone = g->screen == APP_SCREEN_BOOTH && !g->camera_present;
    g->camera_gone_ticks = gone ? (uint16_t)(g->camera_gone_ticks + 1) : 0;
    g->booth.view.unplugged = gone;
    if (g->camera_gone_ticks >= APP_CAMERA_GONE_S * TICKS_PER_SECOND) {
        g->camera_gone_ticks = 0;
        set_screen(g, APP_SCREEN_HOME);
    }
    if (!booth_open(g)) {
        booth_cancel(&g->booth);
        return;
    }
    if (booth_capturing(g)) {
        g->screen_idle_ticks = 0;   /* Holding still for a photo is not idling. */
    }
    const app_booth_phase_t before = g->booth.view.phase;
    g->events |= booth_update(&g->booth);
    const app_booth_phase_t after = g->booth.view.phase;
    if (before != after && (after == APP_BOOTH_FLASH || after == APP_BOOTH_RECORD)) {
        react(g, APP_MOOD_SURPRISED, REACT_SURPRISE_TICKS);
    }
}

static app_mood_t compute_mood(const struct app_game *g)
{
    if (g->power != APP_POWER_AWAKE) {
        return APP_MOOD_SLEEPY;   /* Dozing off with the screen. */
    }
    if (g->fainted) {
        return APP_MOOD_FAINTED;
    }
    if (g->reaction_ticks) {
        return g->reaction;
    }
    if (g->walk_ticks < WALK_MOOD_TICKS) {
        return APP_MOOD_WALK;
    }
    if (g->idle_ticks > SLEEP_AFTER_TICKS) {
        return APP_MOOD_SLEEPY;
    }
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_ARCADE
            && g->arcade == APP_ARCADE_CARDS && g->cards.view.phase == APP_CARDS_GUESS) {
        return APP_MOOD_THINKING;
    }
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_ARCADE
            && g->arcade == APP_ARCADE_SLOTS && g->slots.view.phase == APP_SLOTS_SPINNING) {
        return APP_MOOD_THINKING;
    }
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_ARCADE
            && g->arcade == APP_ARCADE_GRID && g->grid.view.phase == APP_GRID_PLAYING) {
        return APP_MOOD_THINKING;
    }
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_ARCADE
            && g->arcade == APP_ARCADE_BITS && g->bits.view.phase == APP_BITS_PLAYING) {
        return APP_MOOD_THINKING;
    }
    if (booth_open(g) && g->booth.view.phase == APP_BOOTH_COUNTDOWN) {
        return APP_MOOD_THINKING;   /* Holding still for the photo. */
    }
    if (fullness(g) < LOW_STAT) {
        return APP_MOOD_HUNGRY;
    }
    if (g->happiness < LOW_STAT) {
        return APP_MOOD_SAD;
    }
    return APP_MOOD_IDLE;
}

static void decay_stats(app_game_handle_t g)
{
    if (++g->snack_timer >= SNACK_DECAY_TICKS) {
        g->snack_timer = 0;
        if (g->snack) {
            g->snack = add_stat(g->snack, -1);
            changed(g);
        }
    }
    const uint32_t happy_period = fullness(g) < LOW_STAT ? HAPPY_DECAY_TICKS / 2 : HAPPY_DECAY_TICKS;
    if (++g->happy_timer >= happy_period) {
        g->happy_timer = 0;
        g->happiness = add_stat(g->happiness, -1);
        changed(g);
    }
}

void app_game_update(app_game_handle_t g)
{
    if (!g || g->paused) {
        return;
    }
    ++g->tick;
    if (g->walk_ticks < WALK_MOOD_TICKS) {
        ++g->walk_ticks;
    }
    if (!g->pointer_down && g->idle_ticks <= SLEEP_AFTER_TICKS) {
        ++g->idle_ticks;
    }
    if (g->reaction_ticks) {
        --g->reaction_ticks;
    }
    if (g->pet_cooldown) {
        --g->pet_cooldown;
    }
    decay_stats(g);
    update_booth(g);
    update_power(g);
    update_reset(g);
    const uint32_t won = cards_update(&g->cards, &g->events);
    if (won) {
        g->sparks += won;
        react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS);
        changed(g);
    }
    grid_update(&g->grid);
    const uint32_t before_bits = g->events;
    const uint32_t bits_won = bits_update(&g->bits, &g->events);
    if ((g->events & ~before_bits) & APP_EVENT_BIT_DONE) {
        g->sparks += bits_won;
        if (bits_won) {
            react(g, APP_MOOD_HAPPY, bits_won >= APP_BITS_COST * 2 ? REACT_GOAL_TICKS : REACT_WIN_TICKS);
        } else {
            react(g, APP_MOOD_SAD, REACT_SURPRISE_TICKS);
        }
        changed(g);
    }
    const uint32_t before = g->events;
    const uint32_t slot_win = slots_update(&g->slots, &g->events);
    if ((g->events & ~before) & APP_EVENT_SLOT_DONE) {
        g->sparks += slot_win;
        if (slot_win >= SLOTS_BIG_WIN) {
            react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS);
        } else if (slot_win) {
            react(g, APP_MOOD_HAPPY, REACT_PET_TICKS);
        } else {
            react(g, APP_MOOD_SAD, REACT_SURPRISE_TICKS);
        }
        changed(g);
    }
    g->mood = compute_mood(g);
}

app_game_snapshot_t app_game_read(app_game_handle_t g)
{
    if (!g) {
        return (app_game_snapshot_t) {
            0
        };
    }
    app_game_snapshot_t snap = (app_game_snapshot_t) {
        .tick = g->tick,
        .pointer_x = g->pointer_x,
        .pointer_y = g->pointer_y,
        .pointer_down = g->pointer_down,
        .paused = g->paused,
        .screen = g->screen,
        .overlay = g->overlay,
        .arcade = g->arcade,
        .store_page = g->store_page,
        .power = g->power,
        .battery_pct = g->battery_pct,
        .battery_charging = g->battery_charging,
        .mood = g->mood,
        .owned = g->owned,
        .filters_owned = g->filters_owned,
        .lenses_owned = g->lenses_owned,
        .anims_on = g->anims_on,
        .sound = g->sound,
        .settings_page = g->settings_page,
        .sleep_s = s_sleep_s[g->sleep_idx < APP_SLEEP_OPTIONS ? g->sleep_idx : DEFAULT_SLEEP_IDX],
        .brightness_pct = s_bright_pct[g->bright_idx < APP_BRIGHT_OPTIONS ? g->bright_idx : DEFAULT_BRIGHT_IDX],
        .reset_hold = (uint8_t)(g->reset_hold_ticks * 100U / (APP_RESET_HOLD_S * TICKS_PER_SECOND)),
        .steps_total = g->steps_total,
        .steps_today = g->steps_today,
        .step_goal = g->step_goal,
        .sparks = g->sparks,
        .hunger = fullness(g),
        .food_need = food_need(g),
        .hp = g->hp,
        .fainted = g->fainted,
        .fed_today = g->fed_today,
        .fed_streak = g->fed_streak,
        .day = g->day,
        .net = g->net,
        .ipv4 = g->ipv4,
        .camera_present = g->camera_present,
        .happiness = g->happiness,
        .friendship = g->friendship,
        .pixel_style = g->pixel_style,
        .color_index = g->color_index,
        .last_food = g->last_food,
        .cards = g->cards.view,
        .slots = g->slots.view,
        .grid = g->grid.view,
        .bits = g->bits.view,
        .arcade_row = g->arcade_row,
        .booth = g->booth.view,
        .save_seq = g->save_seq,
    };
    memcpy(snap.history, g->history, sizeof(snap.history));
    memcpy(snap.setup_ap, g->setup_ap, sizeof(snap.setup_ap));
    return snap;
}

uint32_t app_game_take_events(app_game_handle_t g)
{
    if (!g) {
        return 0;
    }
    const uint32_t events = g->events;
    g->events = 0;
    return events;
}

const app_food_t *app_game_food(int index)
{
    return index >= 0 && index < APP_FOOD_COUNT ? &s_foods[index] : NULL;
}

const app_store_item_t *app_game_store_item(int index)
{
    return index >= 0 && index < STORE_COUNT ? &s_store[index] : NULL;
}

int app_game_store_count(void)
{
    return STORE_COUNT;
}

int app_game_store_visible_count(bool camera)
{
    int n = 0;
    for (int i = 0; i < STORE_COUNT; ++i) {
        n += item_visible(&s_store[i], camera);
    }
    return n;
}

int app_game_store_visible(bool camera, int position)
{
    for (int i = 0; i < STORE_COUNT && position >= 0; ++i) {
        if (item_visible(&s_store[i], camera) && position-- == 0) {
            return i;
        }
    }
    return -1;
}

int app_game_store_find(app_item_kind_t kind, uint8_t value)
{
    for (int i = 0; i < STORE_COUNT; ++i) {
        if (s_store[i].kind == kind && s_store[i].value == value) {
            return i;
        }
    }
    return -1;
}

void app_game_export(app_game_handle_t g, app_game_record_t *r)
{
    if (!g || !r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->steps_total = g->steps_total;
    r->steps_today = g->steps_today;
    r->day = g->day;
    r->sparks = g->sparks;
    r->spark_progress = g->spark_progress;
    r->friendship = g->friendship;
    r->hunger = g->snack;
    r->hp = g->hp;
    r->fainted = g->fainted;
    r->fed_today = g->fed_today;
    memcpy(r->history, g->history, sizeof(r->history));
    r->fed_streak = g->fed_streak;
    r->happiness = g->happiness;
    r->pixel_style = (uint8_t)g->pixel_style;
    r->color_index = g->color_index;
    r->best_streak = g->cards.view.best_streak;
    r->goal_awarded = g->goal_awarded;
    r->owned = g->owned;
    r->anims_on = g->anims_on;
    r->sound = (uint8_t)g->sound;
    r->bits_best = g->bits.view.best;
    r->sleep_code = g->sleep_idx == DEFAULT_SLEEP_IDX ? 0 : (uint8_t)(g->sleep_idx + 1);
    r->bright_code = g->bright_idx == DEFAULT_BRIGHT_IDX ? 0 : (uint8_t)(g->bright_idx + 1);
    r->goal_k = (uint8_t)(g->step_goal / 1000U);
    r->filters_owned = g->filters_owned;
    r->filter = g->booth.view.filter;
    r->lenses_owned = g->lenses_owned;
    r->lens = (uint8_t)(g->booth.view.res | (g->booth.view.color ? APP_RECORD_LENS_COLOR : 0));
}

void app_game_import(app_game_handle_t g, const app_game_record_t *r)
{
    if (!g || !r) {
        return;
    }
    g->steps_total = r->steps_total;
    g->steps_today = r->steps_today;
    g->day = r->day > 0 ? r->day : 0;
    g->sparks = r->sparks;
    g->spark_progress = r->spark_progress < STEPS_PER_SPARK ? r->spark_progress : 0;
    g->friendship = r->friendship > FRIENDSHIP_MAX ? FRIENDSHIP_MAX : r->friendship;
    g->snack = r->hunger > STAT_MAX ? STAT_MAX : r->hunger;
    g->hp = r->hp > APP_HP_MAX ? APP_HP_MAX : r->hp;
    g->fainted = r->fainted && g->hp == 0;
    g->fed_today = r->fed_today != 0;
    memcpy(g->history, r->history, sizeof(g->history));
    g->fed_streak = r->fed_streak;
    g->happiness = r->happiness > STAT_MAX ? STAT_MAX : r->happiness;
    /* Only owned things can be selected or switched on. */
    g->owned = r->owned | FREE_OWNED;
    uint8_t anims = 0;
    for (int i = 0; i < APP_ANIM_COUNT; ++i) {
        if ((r->anims_on & (1U << i)) && (g->owned & APP_OWN_ANIM_BIT(i))) {
            anims |= (uint8_t)(1U << i);
        }
    }
    g->anims_on = anims;
    g->sound = r->sound < APP_SOUND_COUNT ? (app_sound_t)r->sound : APP_SOUND_MID;
    g->bits.view.best = r->bits_best;
    g->sleep_idx = r->sleep_code && r->sleep_code <= APP_SLEEP_OPTIONS
                   ? (uint8_t)(r->sleep_code - 1) : DEFAULT_SLEEP_IDX;
    g->bright_idx = r->bright_code && r->bright_code <= APP_BRIGHT_OPTIONS
                    ? (uint8_t)(r->bright_code - 1) : DEFAULT_BRIGHT_IDX;
    g->filters_owned = (uint8_t)(r->filters_owned | FREE_FILTERS);
    g->lenses_owned = (uint8_t)((r->lenses_owned & (((1U << APP_PHOTO_RES_COUNT) - 1U) | APP_OWN_FILM_BIT))
                                | FREE_LENSES);
    const uint8_t lens = (uint8_t)(r->lens & ~APP_RECORD_LENS_COLOR);
    booth_select_lens(&g->booth, lens < APP_PHOTO_RES_COUNT
                      && (g->lenses_owned & APP_OWN_LENS_BIT(lens)) ? lens : APP_PHOTO_RES_LOW);
    booth_select_color(&g->booth, (r->lens & APP_RECORD_LENS_COLOR) && (g->lenses_owned & APP_OWN_FILM_BIT));
    booth_select_filter(&g->booth, r->filter < APP_FILTER_COUNT
                        && (g->filters_owned & APP_OWN_FILTER_BIT(r->filter)) ? r->filter : APP_FILTER_NONE);
    g->pixel_style = r->pixel_style < APP_PIXEL_COUNT && (g->owned & APP_OWN_STYLE_BIT(r->pixel_style))
                     ? (app_pixel_style_t)r->pixel_style : APP_PIXEL_CHUNKY;
    g->color_index = r->color_index < APP_COLOR_COUNT && (g->owned & APP_OWN_COLOR_BIT(r->color_index))
                     ? r->color_index : 0;
    const uint32_t goal = r->goal_k * 1000UL;
    g->step_goal = goal >= APP_GOAL_MIN && goal <= APP_GOAL_MAX ? goal : APP_GOAL_DEFAULT;
    g->cards.view.best_streak = r->best_streak;
    g->goal_awarded = r->goal_awarded != 0;
    if ((int)g->screen > last_screen(g)) {
        g->screen = APP_SCREEN_HOME;
    }
    g->events |= APP_EVENT_SETTINGS;
}
