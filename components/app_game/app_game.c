// SPDX-License-Identifier: Apache-2.0
/* Pet model: steps -> sparks, stats, mood, screen navigation and touch rules.
 * Pure C; drawing and hardware live in app_game_runtime. */
#include <stdlib.h>
#include <string.h>
#include "include/app_game.h"
#include "include/app_game_layout.h"
#include "private/cards.h"
#include "private/pedometer.h"
#include "private/slots.h"

#define TICKS_PER_SECOND 30U
enum {
    STEPS_PER_SPARK = APP_STEPS_PER_SPARK,
    START_SPARKS = 30,
    START_HUNGER = 70,
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
    HUNGER_DECAY_TICKS = 120 * TICKS_PER_SECOND,
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
    {APP_ITEM_GAME, APP_GAME_SLOTS, 250, "SPARK SLOTS", "STOP THE REELS TO WIN"},
    {APP_ITEM_ANIM, APP_ANIM_RAINBOW, 300, "RAINBOW", "COLORS WHILE WALKING"},
};
#define STORE_COUNT ((int)(sizeof(s_store) / sizeof(s_store[0])))
#define STORE_PAGES ((STORE_COUNT + APP_STORE_PER_PAGE - 1) / APP_STORE_PER_PAGE)
#define FREE_OWNED (APP_OWN_COLOR_BIT(0) | APP_OWN_STYLE_BIT(APP_PIXEL_CHUNKY))
#define DANCE_EVERY_STEPS 100U

struct app_game {
    app_game_config_t config;
    uint32_t tick;
    bool paused;
    int32_t pointer_x, pointer_y;
    bool pointer_down;
    int32_t press_x, press_y;
    app_screen_t screen;
    app_overlay_t overlay;
    uint8_t store_page;
    uint32_t owned;
    uint8_t anims_on;
    uint32_t step_goal;
    app_power_t power;
    uint32_t screen_idle_ticks;   /* Touch-free ticks; steps do not reset it. */
    bool swallow_touch;           /* The touch that woke the screen does nothing else. */
    int8_t battery_pct;
    bool battery_charging;
    uint32_t events;
    uint32_t save_seq;

    uint32_t steps_total, steps_today, sparks;
    int32_t day;
    uint16_t spark_progress, friendship;
    uint8_t hunger, happiness;
    bool goal_awarded;
    app_pixel_style_t pixel_style;
    uint8_t color_index;
    int8_t last_food;

    uint32_t idle_ticks, walk_ticks, hunger_timer, happy_timer;
    app_mood_t reaction;
    uint16_t reaction_ticks, pet_cooldown;
    app_mood_t mood;

    pedometer_t pedometer;
    bool has_sensor, sensor_valid;
    uint32_t sensor_last;
    cards_t cards;
    slots_t slots;
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
    g->store_page = 0;
    g->owned = FREE_OWNED;
    g->anims_on = 0;
    g->step_goal = APP_GOAL_DEFAULT;
    g->power = APP_POWER_AWAKE;
    g->screen_idle_ticks = 0;
    g->swallow_touch = false;
    g->steps_total = g->steps_today = 0;
    g->day = 0;
    g->sparks = START_SPARKS;
    g->spark_progress = 0;
    g->friendship = 0;
    g->hunger = START_HUNGER;
    g->happiness = START_HAPPINESS;
    g->goal_awarded = false;
    g->pixel_style = APP_PIXEL_CHUNKY;
    g->color_index = 0;
    g->last_food = -1;
    g->idle_ticks = 0;
    g->walk_ticks = WALK_MOOD_TICKS;
    g->hunger_timer = g->happy_timer = 0;
    g->reaction_ticks = g->pet_cooldown = 0;
    g->mood = APP_MOOD_IDLE;
    g->cards.view.best_streak = 0;
    cards_init(&g->cards, g->cards.rng);
    slots_init(&g->slots, g->slots.rng);
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

void app_game_set_paused(app_game_handle_t g, bool paused)
{
    if (g) {
        g->paused = paused;
    }
}

static int last_screen(const struct app_game *g)
{
    return (g->owned & APP_OWN_GAME_BIT(APP_GAME_SLOTS)) ? APP_SCREEN_SLOTS : APP_SCREEN_CARDS;
}

static void go_screen(app_game_handle_t g, int delta)
{
    int next = (int)g->screen + delta;
    if (next < 0 || next > last_screen(g)) {
        return;
    }
    g->screen = (app_screen_t)next;
    g->events |= APP_EVENT_SCREEN;
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

static void tap_feed(app_game_handle_t g, int32_t x, int32_t y)
{
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
        g->hunger = add_stat(g->hunger, food->hunger);
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
    if (g->screen != APP_SCREEN_HOME) {
        g->screen = APP_SCREEN_HOME;
        g->events |= APP_EVENT_SCREEN;
    }
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
    return (low ? APP_LOW_BATTERY_DOZE_S : APP_DOZE_AFTER_S) * TICKS_PER_SECOND;
}

static void update_power(app_game_handle_t g)
{
    if (g->power == APP_POWER_DOZE || g->pointer_down) {
        return;
    }
    ++g->screen_idle_ticks;
    const uint32_t doze_at = doze_after_ticks(g);
    const uint32_t dim_at = APP_DIM_AFTER_S * TICKS_PER_SECOND < doze_at
                            ? APP_DIM_AFTER_S * TICKS_PER_SECOND : doze_at * 2U / 3U;
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
    default: return APP_OWN_ANIM_BIT(item->value);
    }
}

static void open_game(app_game_handle_t g)
{
    set_overlay(g, APP_OVERLAY_NONE);
    if (g->screen != APP_SCREEN_SLOTS) {
        g->screen = APP_SCREEN_SLOTS;
        g->events |= APP_EVENT_SCREEN;
    }
}

static void open_store_at(app_game_handle_t g, app_item_kind_t kind, uint8_t value)
{
    const int index = app_game_store_find(kind, value);
    if (index >= 0) {
        g->store_page = (uint8_t)(index / APP_STORE_PER_PAGE);
    }
    set_overlay(g, APP_OVERLAY_STORE);
}

/** Select an owned color/style, or toggle an owned animation. */
static void equip(app_game_handle_t g, const app_store_item_t *item, bool just_bought)
{
    switch (item->kind) {
    case APP_ITEM_COLOR: g->color_index = item->value; break;
    case APP_ITEM_STYLE: g->pixel_style = (app_pixel_style_t)item->value; break;
    case APP_ITEM_GAME: open_game(g); break;
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
        const int index = g->store_page * APP_STORE_PER_PAGE + row;
        if (index >= STORE_COUNT) {
            return;
        }
        if (!app_rect_hit(app_store_row_rect(row), x, y)) {
            continue;
        }
        const app_store_item_t *item = &s_store[index];
        const uint32_t bit = item_bit(item);
        if (g->owned & bit) {
            equip(g, item, false);
            return;
        }
        if (g->sparks < item->cost) {
            react(g, APP_MOOD_GRUMPY, REACT_GRUMPY_TICKS);
            g->events |= APP_EVENT_NO_SPARKS;
            return;
        }
        g->sparks -= item->cost;
        g->owned |= bit;
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
        set_overlay(g, APP_OVERLAY_MENU);
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

static void tap_menu(app_game_handle_t g, int32_t x, int32_t y)
{
    if (app_rect_hit(app_menu_sleep_rect(), x, y)) {
        doze(g);
    } else if (app_rect_hit(app_menu_store_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_STORE);
    } else if (app_rect_hit(app_menu_style_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_STYLE);
    } else if (app_rect_hit(app_menu_goal_rect(), x, y)) {
        set_overlay(g, APP_OVERLAY_GOAL);
    } else if (y >= APP_MENU_H) {
        set_overlay(g, APP_OVERLAY_NONE);
    }
}

static void tap_slots(app_game_handle_t g, int32_t x, int32_t y)
{
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

static void handle_tap(app_game_handle_t g, int32_t x, int32_t y)
{
    switch (g->overlay) {
    case APP_OVERLAY_MENU: tap_menu(g, x, y); return;
    case APP_OVERLAY_STYLE: tap_style(g, x, y); return;
    case APP_OVERLAY_STORE: tap_store(g, x, y); return;
    case APP_OVERLAY_GOAL: tap_goal(g, x, y); return;
    default: break;
    }
    switch (g->screen) {
    case APP_SCREEN_HOME: tap_home(g, x, y); break;
    case APP_SCREEN_FEED: tap_feed(g, x, y); break;
    case APP_SCREEN_CARDS: tap_cards(g, x, y); break;
    case APP_SCREEN_SLOTS: tap_slots(g, x, y); break;
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
        }
        return;
    }
    if (adx < APP_SWIPE_MIN_PX || adx <= ady) {
        return;
    }
    if (g->overlay == APP_OVERLAY_STORE) {
        const int page = (int)g->store_page + (dx < 0 ? 1 : -1);
        if (page >= 0 && page < STORE_PAGES) {
            g->store_page = (uint8_t)page;
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
    if (g->paused) {
        return;
    }
    if (pressed && !was_down) {
        g->press_x = x;
        g->press_y = y;
        g->idle_ticks = 0;
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

void app_game_set_battery(app_game_handle_t g, int percent, bool charging)
{
    if (!g) {
        return;
    }
    g->battery_pct = (int8_t)(percent < 0 ? -1 : (percent > 100 ? 100 : percent));
    g->battery_charging = charging && percent >= 0;
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
    if (g->day > 0) {
        g->steps_today = 0;
        g->goal_awarded = false;
    }
    g->day = day;
    changed(g);
}

static app_mood_t compute_mood(const struct app_game *g)
{
    if (g->power != APP_POWER_AWAKE) {
        return APP_MOOD_SLEEPY;   /* Dozing off with the screen. */
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
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_CARDS
            && g->cards.view.phase == APP_CARDS_GUESS) {
        return APP_MOOD_THINKING;
    }
    if (g->overlay == APP_OVERLAY_NONE && g->screen == APP_SCREEN_SLOTS
            && g->slots.view.phase == APP_SLOTS_SPINNING) {
        return APP_MOOD_THINKING;
    }
    if (g->hunger < LOW_STAT) {
        return APP_MOOD_HUNGRY;
    }
    if (g->happiness < LOW_STAT) {
        return APP_MOOD_SAD;
    }
    return APP_MOOD_IDLE;
}

static void decay_stats(app_game_handle_t g)
{
    if (++g->hunger_timer >= HUNGER_DECAY_TICKS) {
        g->hunger_timer = 0;
        g->hunger = add_stat(g->hunger, -1);
        changed(g);
    }
    const uint32_t happy_period = g->hunger < LOW_STAT ? HAPPY_DECAY_TICKS / 2 : HAPPY_DECAY_TICKS;
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
    update_power(g);
    const uint32_t won = cards_update(&g->cards, &g->events);
    if (won) {
        g->sparks += won;
        react(g, APP_MOOD_HAPPY, REACT_GOAL_TICKS);
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
    return (app_game_snapshot_t) {
        .tick = g->tick,
        .pointer_x = g->pointer_x,
        .pointer_y = g->pointer_y,
        .pointer_down = g->pointer_down,
        .paused = g->paused,
        .screen = g->screen,
        .overlay = g->overlay,
        .store_page = g->store_page,
        .power = g->power,
        .battery_pct = g->battery_pct,
        .battery_charging = g->battery_charging,
        .mood = g->mood,
        .owned = g->owned,
        .anims_on = g->anims_on,
        .steps_total = g->steps_total,
        .steps_today = g->steps_today,
        .step_goal = g->step_goal,
        .sparks = g->sparks,
        .hunger = g->hunger,
        .happiness = g->happiness,
        .friendship = g->friendship,
        .pixel_style = g->pixel_style,
        .color_index = g->color_index,
        .last_food = g->last_food,
        .cards = g->cards.view,
        .slots = g->slots.view,
        .save_seq = g->save_seq,
    };
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
    r->hunger = g->hunger;
    r->happiness = g->happiness;
    r->pixel_style = (uint8_t)g->pixel_style;
    r->color_index = g->color_index;
    r->best_streak = g->cards.view.best_streak;
    r->goal_awarded = g->goal_awarded;
    r->owned = g->owned;
    r->anims_on = g->anims_on;
    r->goal_k = (uint8_t)(g->step_goal / 1000U);
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
    g->hunger = r->hunger > STAT_MAX ? STAT_MAX : r->hunger;
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
