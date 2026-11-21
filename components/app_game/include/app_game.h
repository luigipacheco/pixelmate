// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque pet model owned by one loop; no concurrent or ISR access.
 * Lifecycle, units and timing contracts: ../README.md. */
typedef struct app_game *app_game_handle_t;

typedef enum {
    APP_GAME_OK = 0,
    APP_GAME_INVALID_ARGUMENT,
    APP_GAME_NO_MEMORY,
} app_game_err_t;

typedef enum {
    APP_SCREEN_HOME = 0,
    APP_SCREEN_STEPS,
    APP_SCREEN_FEED,
    APP_SCREEN_CARDS,
    APP_SCREEN_COUNT,
} app_screen_t;

/** Panels that slide down over the swipeable screens. */
typedef enum {
    APP_OVERLAY_NONE = 0,
    APP_OVERLAY_MENU,      /**< Pulled down from the top edge. */
    APP_OVERLAY_STYLE,     /**< Pick owned pixel style and color. */
    APP_OVERLAY_STORE,     /**< Buy colors, styles and animations with sparks. */
    APP_OVERLAY_GOAL,      /**< Adjust the daily step goal. */
    APP_OVERLAY_COUNT,
} app_overlay_t;

typedef enum {
    APP_MOOD_IDLE = 0,
    APP_MOOD_HAPPY,
    APP_MOOD_SLEEPY,
    APP_MOOD_HUNGRY,
    APP_MOOD_SAD,
    APP_MOOD_WALK,
    APP_MOOD_EATING,
    APP_MOOD_THINKING,
    APP_MOOD_SURPRISED,
    APP_MOOD_GRUMPY,
    APP_MOOD_COUNT,
} app_mood_t;

typedef enum {
    APP_PIXEL_CHUNKY = 0,  /**< 12 px cells with LCD gaps. */
    APP_PIXEL_FINE,        /**< 6 px cells with LCD gaps. */
    APP_PIXEL_SMOOTH,      /**< 2 px cells, no gaps. */
    APP_PIXEL_COUNT,
} app_pixel_style_t;

enum {
    APP_COLOR_COUNT = 6,
    APP_FOOD_COUNT = 3,
    APP_CARDS_DEAL_COST = 5,     /**< Sparks. */
    APP_STEPS_PER_SPARK = 20,
    APP_GOAL_DEFAULT = 8000,     /**< Steps per day. */
    APP_GOAL_MIN = 1000,
    APP_GOAL_MAX = 30000,
    APP_GOAL_STEP = 1000,        /**< Adjustment per +/- tap. */
};

/** Unlockable animations, bought in the store and toggled on/off. */
typedef enum {
    APP_ANIM_TRAIL = 0,    /**< Sparks fall from the eyes on every step. */
    APP_ANIM_DANCE,        /**< A little dance every 100 steps. */
    APP_ANIM_STARS,        /**< Star-shaped eyes when happy. */
    APP_ANIM_RAINBOW,      /**< Color-cycling eyes while walking. */
    APP_ANIM_COUNT,
} app_anim_t;

typedef enum {
    APP_ITEM_COLOR = 0,
    APP_ITEM_STYLE,
    APP_ITEM_ANIM,
} app_item_kind_t;

typedef struct {
    app_item_kind_t kind;
    uint8_t value;         /**< Color index, app_pixel_style_t or app_anim_t. */
    uint16_t cost;         /**< Sparks. */
    const char *name;      /**< Upper-case, pixel-font drawable. */
    const char *blurb;     /**< One short upper-case line. */
} app_store_item_t;

enum { APP_STORE_PER_PAGE = 4 };

/* Ownership bit layout in app_game_snapshot_t.owned. */
#define APP_OWN_COLOR_BIT(i) (1UL << (i))
#define APP_OWN_STYLE_BIT(i) (1UL << (8 + (i)))
#define APP_OWN_ANIM_BIT(i)  (1UL << (16 + (i)))

typedef enum {
    APP_CARDS_READY = 0,   /**< Waiting for a paid deal. */
    APP_CARDS_GUESS,       /**< Next card face down, waiting for HI/LO/cash. */
    APP_CARDS_REVEAL,      /**< Next card shown; result timer running. */
} app_cards_phase_t;

typedef enum {
    APP_CARD_RESULT_NONE = 0,
    APP_CARD_RESULT_WIN,
    APP_CARD_RESULT_LOSE,
    APP_CARD_RESULT_TIE,
} app_card_result_t;

/** One-shot event bits, accumulated until app_game_take_events(). */
enum {
    APP_EVENT_STEP = 1U << 0,
    APP_EVENT_SPARK = 1U << 1,
    APP_EVENT_GOAL = 1U << 2,
    APP_EVENT_ATE = 1U << 3,
    APP_EVENT_NO_SPARKS = 1U << 4,
    APP_EVENT_PETTED = 1U << 5,
    APP_EVENT_CARD_DEAL = 1U << 6,
    APP_EVENT_CARD_REVEAL = 1U << 7,
    APP_EVENT_CARD_NEXT = 1U << 8,
    APP_EVENT_CASH_OUT = 1U << 9,
    APP_EVENT_SCREEN = 1U << 10,
    APP_EVENT_SETTINGS = 1U << 11,
    APP_EVENT_WOKE = 1U << 12,
    APP_EVENT_OVERLAY = 1U << 13,
    APP_EVENT_BOUGHT = 1U << 14,
    APP_EVENT_DANCE = 1U << 15,
    APP_EVENT_STORE_PAGE = 1U << 16,
};

/** Events that change spendable or configured data and deserve a prompt save. */
#define APP_EVENTS_SAVE_NOW (APP_EVENT_ATE | APP_EVENT_CARD_DEAL | APP_EVENT_CARD_REVEAL | \
                             APP_EVENT_CASH_OUT | APP_EVENT_SETTINGS | APP_EVENT_GOAL | \
                             APP_EVENT_BOUGHT)

typedef struct {
    const char *name;   /**< Upper-case label drawable by the pixel font. */
    uint16_t cost;      /**< Sparks. */
    uint8_t hunger;     /**< Fullness added, 0..100. */
    uint8_t happiness;  /**< Happiness added, 0..100. */
} app_food_t;

typedef struct {
    app_cards_phase_t phase;
    uint8_t current;            /**< Face-up card, 1..13 (A..K). */
    uint8_t next;               /**< Hidden until REVEAL, 1..13. */
    uint16_t pot;               /**< Sparks at stake. */
    uint8_t streak;             /**< Correct guesses this run. */
    uint8_t best_streak;
    app_card_result_t result;   /**< Result of the last reveal. */
} app_cards_snapshot_t;

typedef struct {
    uint32_t tick;              /**< Update count at 30 Hz, wraps modulo 2^32. */
    int32_t pointer_x;          /**< Pixels from left edge, 0..width-1. */
    int32_t pointer_y;          /**< Pixels from top edge, 0..height-1. */
    bool pointer_down;
    bool paused;
    app_screen_t screen;
    app_overlay_t overlay;
    uint8_t store_page;         /**< 0..store pages-1. */
    app_mood_t mood;
    uint32_t owned;             /**< APP_OWN_*_BIT flags. */
    uint8_t anims_on;           /**< Bit per app_anim_t; only owned ones can be on. */
    uint32_t steps_total;
    uint32_t steps_today;
    uint32_t step_goal;
    uint32_t sparks;
    uint8_t hunger;             /**< Fullness 0..100 (0 = starving). */
    uint8_t happiness;          /**< 0..100. */
    uint16_t friendship;        /**< 0..999; hearts = friendship / 100. */
    app_pixel_style_t pixel_style;
    uint8_t color_index;        /**< 0..APP_COLOR_COUNT-1. */
    int8_t last_food;           /**< Food index of the last meal, -1 if none. */
    app_cards_snapshot_t cards;
    uint32_t save_seq;          /**< Increments whenever persistent data changes. */
} app_game_snapshot_t;

/** Persistent record: plain data, 32 bytes. Bump the save version on layout change. */
typedef struct {
    uint32_t steps_total;
    uint32_t steps_today;
    int32_t day;                /**< Days since epoch of steps_today, 0 if unknown. */
    uint32_t sparks;
    uint32_t owned;             /**< APP_OWN_*_BIT flags. */
    uint16_t spark_progress;    /**< Steps toward the next spark. */
    uint16_t friendship;
    uint8_t hunger;
    uint8_t happiness;
    uint8_t pixel_style;
    uint8_t color_index;
    uint8_t best_streak;
    uint8_t goal_awarded;
    uint8_t anims_on;
    uint8_t goal_k;             /**< Daily goal in thousands of steps; 0 = default. */
} app_game_record_t;

typedef struct {
    uint16_t width;    /**< Viewport width in pixels, 1..65535. */
    uint16_t height;   /**< Viewport height in pixels, 1..65535. */
    uint32_t seed;     /**< Card shuffle seed; 0 selects a fixed default. */
} app_game_config_t;

/** Create a caller-owned model with a fresh pet; clears *out_handle on failure. */
app_game_err_t app_game_create(const app_game_config_t *config, app_game_handle_t *out_handle);
/** Free the model and clear the handle. NULL pointer/handle is a no-op. */
void app_game_destroy(app_game_handle_t *handle);

/** Restore a fresh pet with default settings; keeps viewport and RNG state. */
void app_game_reset(app_game_handle_t handle);
void app_game_set_paused(app_game_handle_t handle, bool paused);
/** Feed one touch sample (pixels, clamped). Release completes taps and swipes. */
void app_game_set_pointer(app_game_handle_t handle, int32_t x, int32_t y, bool pressed);
/** Feed one accelerometer sample in g (25..100 Hz). Software step detection,
 * used when no hardware pedometer reading is available. */
void app_game_add_accel(app_game_handle_t handle, float x_g, float y_g, float z_g);
/** Report the hardware pedometer's cumulative count. The first reading after
 * boot (or a counter that went backwards) is only a baseline; later increases
 * are credited as steps. Once called, software detection is disabled. */
void app_game_set_sensor_steps(app_game_handle_t handle, uint32_t cumulative);
/** Credit steps counted elsewhere (debug or another source). */
void app_game_add_steps(app_game_handle_t handle, uint32_t steps);
/** Set the local calendar day (days since epoch); a new day resets steps_today.
 * Values <= 0 mean "clock unknown" and are ignored. */
void app_game_set_day(app_game_handle_t handle, int32_t day);
/** Advance one 30 Hz tick unless paused. No rendering or hardware access. */
void app_game_update(app_game_handle_t handle);

app_game_snapshot_t app_game_read(app_game_handle_t handle);
/** Return and clear accumulated APP_EVENT_* bits. */
uint32_t app_game_take_events(app_game_handle_t handle);
/** Food table entry, or NULL when index is out of range. */
const app_food_t *app_game_food(int index);
/** Store catalog entry, or NULL when index is out of range. */
const app_store_item_t *app_game_store_item(int index);
/** Number of store catalog entries. */
int app_game_store_count(void);
/** Catalog index selling this item, or -1 if it is free/not sold. */
int app_game_store_find(app_item_kind_t kind, uint8_t value);

void app_game_export(app_game_handle_t handle, app_game_record_t *record);
/** Load a record; out-of-range fields are clamped. NULL arguments are no-ops. */
void app_game_import(app_game_handle_t handle, const app_game_record_t *record);

#ifdef __cplusplus
}
#endif
