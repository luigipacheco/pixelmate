// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Filter ids (app_photo_filter_t) are shared with the photo codec. Relative
 * path: the Host simulator compiles sources without component include paths. */
#include "../../app_photo/include/app_photo.h"

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
    APP_SCREEN_ARCADE,     /**< Row of game cabinets; a game opens inside it. */
    APP_SCREEN_TOOLS,      /**< Flipper-style sensor tools; using them levels Bitsy up. */
    APP_SCREEN_BOOTH,      /**< Photo booth; a page only while a camera is detected. */
    APP_SCREEN_COUNT,
} app_screen_t;

/** Cabinets in the arcade, left to right. LOBBY = no game open. */
typedef enum {
    APP_ARCADE_LOBBY = 0,
    /* Cabinets in unlock order (store price): cheapest first, best earner last. */
    APP_ARCADE_SLOTS,      /**< Spark Slots, 100. */
    APP_ARCADE_STACK,      /**< Spark Stack, 150. */
    APP_ARCADE_GRID,       /**< Spark Grid, 200. */
    APP_ARCADE_BITS,       /**< Bit Flip, 250. */
    APP_ARCADE_SAYS,       /**< Bitsy Says, 300. */
    APP_ARCADE_CARDS,      /**< HI-LO, 400 (the last unlock). */
    APP_ARCADE_COUNT,
} app_arcade_t;

enum {
    APP_ARCADE_CABINETS = APP_ARCADE_COUNT - 1,
    APP_ARCADE_PER_ROW = 3,      /**< Cabinets per row; more rows scroll vertically. */
    APP_ARCADE_ROWS = (APP_ARCADE_CABINETS + APP_ARCADE_PER_ROW - 1) / APP_ARCADE_PER_ROW,
};

/** Panels that slide down over the swipeable screens. */
typedef enum {
    APP_OVERLAY_NONE = 0,
    APP_OVERLAY_MENU,      /**< Pulled down from the top edge. */
    APP_OVERLAY_STYLE,     /**< Pick owned pixel style and color. */
    APP_OVERLAY_STORE,     /**< Buy colors, styles and animations with sparks. */
    APP_OVERLAY_GOAL,      /**< Adjust the daily step goal. */
    APP_OVERLAY_WIFI,      /**< Wi-Fi status and first-time setup. */
    APP_OVERLAY_SETTINGS,  /**< Sound, goal, Wi-Fi, screen sleep, brightness, about, reset. */
    APP_OVERLAY_ABOUT,     /**< Credits: app, version, author and website. */
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
    APP_MOOD_FAINTED,      /**< Out of health: X eyes until walked back. */
    APP_MOOD_COUNT,
} app_mood_t;

typedef enum {
    APP_PIXEL_CHUNKY = 0,  /**< 12 px cells with LCD gaps. */
    APP_PIXEL_FINE,        /**< 6 px cells with LCD gaps. */
    APP_PIXEL_SMOOTH,      /**< 2 px cells, no gaps. */
    APP_PIXEL_COUNT,
} app_pixel_style_t;

/** Sound volume, set in Settings. Only the runtime plays sound; the model just
 * stores the choice. Values keep the earlier saved meaning (0 was ON, 1 LOUD,
 * 2 OFF), so old saves load unchanged; LOW was added after. */
typedef enum {
    APP_SOUND_MID = 0,     /**< Default (was ON). */
    APP_SOUND_HIGH,        /**< Was LOUD. */
    APP_SOUND_OFF,
    APP_SOUND_LOW,
    APP_SOUND_COUNT,
} app_sound_t;

/* Settings choices. Index 0 of each saved code means "default". */
enum {
    APP_SLEEP_OPTIONS = 4,       /**< Screen sleep: 15 s, 30 s (default), 60 s, 120 s. */
    APP_BRIGHT_OPTIONS = 3,      /**< Drawn brightness: 100 % (default), 80 %, 60 %. */
    APP_SETTINGS_ROWS = 9,
    APP_TAPS_PER_STEP = 10,      /**< TAP STEPS: this many pats count as one step. */
    APP_DEV_SPARKS = 99999,      /**< DEV MODE: sparks shown (and topped up). */
    APP_KONAMI_LEN = 10,         /**< Up Up Down Down Left Right Left Right B A. */
    APP_RESET_HOLD_S = 3,        /**< Hold RESET PET this long to start over. */
};

#define APP_VERSION "1.0.0"
#define APP_AUTHOR "LUIS PACHECO"
#define APP_AUTHOR_URL "WWW.LUIGIPACHECO.COM"
#define APP_PET_NAME "BITSY"     /* The pet's name (the app is PixelMate). */

enum {
    APP_COLOR_COUNT = 6,
    APP_FOOD_COUNT = 3,
    APP_CARDS_DEAL_COST = 5,     /**< Sparks. */
    APP_STEPS_PER_SPARK = 20,
    APP_GOAL_DEFAULT = 8000,     /**< Steps per day. */
    APP_GOAL_MIN = 1000,
    APP_GOAL_MAX = 30000,
    APP_GOAL_STEP = 1000,        /**< Adjustment per +/- tap. */
    APP_HP_MAX = 5,              /**< Health hearts. */
    APP_FOOD_GOAL_DIV = 2,       /**< Daily food = step goal / this. */
};

/** Screen power state. Steps keep counting in every state. */
typedef enum {
    APP_POWER_AWAKE = 0,
    APP_POWER_DIM,         /**< Idle a while: pet dozes off, screen dims. */
    APP_POWER_DOZE,        /**< Screen off, nothing drawn; a tap wakes. */
} app_power_t;

enum {
    APP_DIM_AFTER_S = 20,        /**< Seconds without a touch before dimming. */
    APP_DOZE_AFTER_S = 30,       /**< Seconds without a touch before the screen turns off. */
    APP_LOW_BATTERY_PCT = 15,    /**< At or below: warning icon and quicker dozing. */
    APP_LOW_BATTERY_DOZE_S = 12,
};

/** Network state reported by the runtime (display only). */
typedef enum {
    APP_NET_OFF = 0,       /**< No Wi-Fi hardware or not started (Host). */
    APP_NET_NO_CONFIG,     /**< No network saved (app setup or Vibe Mode). */
    APP_NET_CONNECTING,
    APP_NET_ONLINE,
    APP_NET_FAILED,        /**< Saved network unreachable for a while; still retrying. */
    APP_NET_SETUP,         /**< Setup hotspot running; see snapshot setup_ap. */
} app_net_state_t;

enum {
    APP_HISTORY_DAYS = 7,
    APP_SETUP_AP_MAX = 24,       /**< Setup hotspot name buffer, NUL included. */
};

/** Unlockable animations, bought in the store and toggled on/off. */
typedef enum {
    APP_ANIM_TRAIL = 0,    /**< Sparks fall from the eyes on every step. */
    APP_ANIM_DANCE,        /**< A little dance every 100 steps. */
    APP_ANIM_STARS,        /**< Star-shaped eyes when happy. */
    APP_ANIM_RAINBOW,      /**< Color-cycling eyes while walking. */
    APP_ANIM_COUNT,
} app_anim_t;

/** Unlockable mini games. */
typedef enum {
    APP_GAME_SLOTS = 0,
    APP_GAME_GRID,
    APP_GAME_BITS,
    APP_GAME_PHOTO,        /**< Photo booth page (needs the camera module). */
    APP_GAME_CARDS,        /**< HI-LO (was free before it moved to the store). */
    APP_GAME_SAYS,         /**< Bitsy Says. */
    APP_GAME_STACK,        /**< Spark Stack. */
    APP_GAME_COUNT,
} app_minigame_t;

/* ---- Arcade sound/visual cues: the event bits are all taken, so newer
 * games bump snapshot.cue_seq and set snapshot.cue instead. ---- */
typedef enum {
    APP_CUE_NONE = 0,
    APP_CUE_START,
    APP_CUE_PAD0, APP_CUE_PAD1, APP_CUE_PAD2, APP_CUE_PAD3,   /**< Bitsy Says colors. */
    APP_CUE_DROP,          /**< Spark Stack: bar placed (overhang cut). */
    APP_CUE_PERFECT,       /**< Spark Stack: exact drop. */
    APP_CUE_WIN, APP_CUE_COINS, APP_CUE_LOSE,
    APP_CUE_KONAMI,        /**< A correct Konami input. */
    APP_CUE_KONAMI_MISS,   /**< A wrong one: the code starts over. */
    APP_CUE_DEV_ON, APP_CUE_DEV_OFF,
    /* Easter eggs. */
    APP_CUE_LOVE,          /**< 1000th pat: LOVE EYES unlocked. */
    APP_CUE_COIN_RAIN,     /**< Spark counter tapped 10 times fast. */
    APP_CUE_CREDITS,       /**< Version tapped 7 times on About. */
    APP_CUE_DIZZY,         /**< Shaken hard. */
    APP_CUE_PANIC,         /**< Turned upside down. */
    APP_CUE_MISSED,        /**< Back after 3+ days away. */
    APP_CUE_MARATHON,      /**< 10,000 steps today. */
    APP_CUE_NEW_YEAR,      /**< Midnight, January 1st, on the clock. */
    APP_CUE_TOOL_OPEN,     /**< A tool opened from the TOOLS list. */
    APP_CUE_LEVEL_UP,      /**< Bitsy reached a new tools level. */
    APP_CUE_CAL_DONE,      /**< Step calibration saved. */
    APP_CUE_METAL,         /**< Metal detector tick (rate follows the field). */
    APP_CUE_TOOL_ACTION,   /**< A tool button (scan, send, arm...). */
    APP_CUE_ALARM,         /**< GUARD MODE saw motion. */
} app_cue_t;

enum {
    APP_LOVE_PATS = 1000,        /**< Pats that unlock LOVE EYES. */
    APP_MARATHON_STEPS = 10000,  /**< Steps in a day for the MARATHON headband. */
    APP_CREDITS_TICKS = 600,     /**< Credits roll length (20 s). */
};

/* ---- TOOLS: Flipper-style sensor apps ---- */
typedef enum {
    APP_TOOL_LIST = 0,     /**< The grid of tools. */
    APP_TOOL_SENSORS,      /**< Live graphs of every sensor. */
    APP_TOOL_STEPCAL,      /**< Walk 50 counted steps to calibrate the step counter. */
    APP_TOOL_COMPASS,      /**< Compass, or metal detector (tap to switch). */
    APP_TOOL_LEVEL,        /**< Bubble level. */
    APP_TOOL_SEISMO,       /**< Seismograph from the accelerometer. */
    APP_TOOL_WIFI,         /**< Wi-Fi scanner. */
    APP_TOOL_IR,           /**< IR remote (interaction module). */
    APP_TOOL_GUARD,        /**< Motion guard (interaction module). */
    APP_TOOL_LIGHT,        /**< Light meter / flashlight (interaction module). */
    APP_TOOL_COUNT,
} app_tool_t;

typedef enum {
    APP_CAL_IDLE = 0, APP_CAL_WALKING, APP_CAL_DONE,
} app_cal_phase_t;

enum {
    APP_CAL_STEPS = 50,          /**< Steps the user walks (and counts) to calibrate. */
    APP_TOOLS_XP_PER_LEVEL = 10,
    APP_SENSOR_PAGES = 5,        /**< ACCEL, GYRO, MAGNET, POWER, STEPS. */
    APP_IR_BUTTONS = 6,
};

typedef struct {
    uint8_t tool;                /**< app_tool_t open now (LIST = the grid). */
    uint8_t sensor_page;         /**< SENSORS: which graph. */
    bool metal_mode;             /**< COMPASS: metal detector instead of the needle. */
    uint8_t cal_phase;           /**< app_cal_phase_t. */
    uint16_t cal_measured;       /**< Raw counter steps since START. */
    uint8_t step_cal_pct;        /**< Step counter correction, 100 = none (saved). */
    uint16_t xp;                 /**< Tools XP (saved); level = 1 + xp / APP_TOOLS_XP_PER_LEVEL. */
    uint8_t scan_seq;            /**< WI-FI: bumped to ask the runtime for a scan. */
    uint8_t level_zero_seq;      /**< LEVEL: bumped to zero the bubble here. */
    uint8_t ir_seq, ir_button;   /**< IR: bumped with the button to send. */
    bool guard_armed;            /**< GUARD: watching for motion. */
    uint16_t guard_hits;         /**< GUARD: motion seen while armed. */
    bool flashlight;             /**< LIGHT: LEDs on as a flashlight. */
    bool module_present;         /**< Interaction module plugged in (runtime). */
} app_tools_snapshot_t;

/* ---- Bitsy Says: repeat a growing sequence of four colors ---- */
enum {
    APP_SAYS_PADS = 4,
    APP_SAYS_MAX = 32,           /**< Longest sequence (a perfect run). */
    APP_SAYS_COST = 5,
    APP_SAYS_PAYOUT_MAX = 30,    /**< A run pays 1 spark per completed round, up to this. */
};

typedef enum {
    APP_SAYS_READY = 0, APP_SAYS_SHOW, APP_SAYS_INPUT, APP_SAYS_RESULT,
} app_says_phase_t;

typedef struct {
    app_says_phase_t phase;
    uint8_t round;               /**< Sequence length this round. */
    uint8_t shown;               /**< SHOW: colors shown so far. */
    uint8_t entered;             /**< INPUT: colors repeated so far. */
    int8_t lit;                  /**< Pad glowing now, -1 none. */
    int8_t wrong;                /**< Pad tapped by mistake, -1 none (timeout). */
    uint16_t wait;               /**< SHOW: ticks before the first color. */
    uint8_t payout, best;        /**< Last run's sparks; best rounds (saved). */
    bool failed;
} app_says_snapshot_t;

/* ---- Spark Stack: drop sliding bars into a tower ---- */
enum {
    APP_STACK_COLS = 12,
    APP_STACK_ROWS = 12,         /**< Reach the top for a bonus. */
    APP_STACK_START_W = 5,
    APP_STACK_COST = 5,
    APP_STACK_PAYOUT_MAX = 40,
};

typedef enum {
    APP_STACK_READY = 0, APP_STACK_PLAYING, APP_STACK_RESULT,
} app_stack_phase_t;

typedef struct {
    app_stack_phase_t phase;
    uint8_t rows;                        /**< Rows placed. */
    uint8_t left[APP_STACK_ROWS], row_w[APP_STACK_ROWS];
    uint8_t width;                       /**< Moving bar width (cells). */
    int16_t pos_q;                       /**< Moving bar left edge, 1/16 cells. */
    int8_t dir;
    uint8_t speed_q;
    uint8_t perfects, payout, best;      /**< best = most rows (saved). */
    bool topped;
    uint8_t cut_left, cut_w, cut_row;    /**< Last overhang that fell off. */
    uint8_t drop_seq;                    /**< Bumped on every drop (view animation). */
} app_stack_snapshot_t;

/* ---- Bit Flip: set 8 switches (128..1) to the target before time runs out ---- */
enum {
    APP_BITS = 8,
    APP_BITS_COST = 5,           /**< Sparks per run. */
    APP_BITS_PAYOUT_MAX = 30,    /**< A run pays 1 spark per hit, up to this. */
};

typedef enum {
    APP_BITS_READY = 0,
    APP_BITS_PLAYING,
    APP_BITS_RESULT,             /**< Run over; payout shown. */
} app_bits_phase_t;

typedef struct {
    app_bits_phase_t phase;
    uint8_t target;              /**< 1..255. */
    uint8_t switches;            /**< Bit 7 = the 128 switch (leftmost). */
    uint16_t hits;               /**< Correct answers this run. */
    uint16_t best;               /**< Best run ever (saved). */
    uint16_t time_left;          /**< Ticks left for this target. */
    uint16_t time_limit;         /**< Ticks this target started with. */
    uint16_t payout;             /**< Sparks paid by the last run. */
    bool last_was_hit;           /**< The last flip completed the target. */
} app_bits_snapshot_t;

/* ---- Spark Grid: flip tiles, multiply the score, avoid duds ---- */
enum {
    APP_GRID_N = 5,              /**< Tiles per row and column. */
    APP_GRID_TILES = 25,
    APP_GRID_COST = 10,          /**< Sparks per round. */
    APP_GRID_LEVELS = 5,
    APP_GRID_DUD = 0,            /**< Tile value of a dud; others are 1..3. */
};

typedef enum {
    APP_GRID_READY = 0,          /**< Waiting for a paid round. */
    APP_GRID_PLAYING,
    APP_GRID_RESULT,             /**< Board shown; next round soon. */
} app_grid_phase_t;

typedef enum {
    APP_GRID_RESULT_NONE = 0,
    APP_GRID_RESULT_CLEARED,     /**< Every x2/x3 found: auto cash-out. */
    APP_GRID_RESULT_CASHED,
    APP_GRID_RESULT_DUD,         /**< Hit a dud: score lost. */
} app_grid_result_t;

typedef struct {
    app_grid_phase_t phase;
    uint8_t tiles[APP_GRID_TILES];     /**< 0 = dud, 1..3; row-major. Show only revealed ones. */
    uint32_t revealed;                 /**< Bit per tile. */
    uint8_t row_sum[APP_GRID_N], row_duds[APP_GRID_N];
    uint8_t col_sum[APP_GRID_N], col_duds[APP_GRID_N];
    uint16_t score;                    /**< Product of flipped values; payout on cash. */
    uint16_t payout;                   /**< Sparks paid by the last round. */
    int8_t last_flip;                  /**< Tile flipped most recently, -1 if none. */
    app_grid_result_t result;
    uint8_t level;                     /**< 1..APP_GRID_LEVELS: more x2/x3 and more duds. */
    uint8_t flips;                     /**< Good tiles flipped this round (sets the level drop). */
    uint8_t marks[APP_GRID_TILES];     /**< app_grid_mark_t memo on hidden tiles. */
} app_grid_snapshot_t;

/** Memo marks, cycled by a long press on a hidden tile. A DUD mark guards
 * the tile against an accidental tap. */
typedef enum {
    APP_GRID_MARK_NONE = 0,
    APP_GRID_MARK_DUD,
    APP_GRID_MARK_1,
    APP_GRID_MARK_2,
    APP_GRID_MARK_3,
    APP_GRID_MARK_COUNT,
} app_grid_mark_t;

typedef enum {
    APP_ITEM_COLOR = 0,
    APP_ITEM_STYLE,
    APP_ITEM_ANIM,
    APP_ITEM_GAME,
    APP_ITEM_FILTER,       /**< Photo look (app_photo_filter_t); needs the booth. */
    APP_ITEM_LENS,         /**< Photo resolution (app_photo_res_t); needs the booth. */
    APP_ITEM_FILM,         /**< COLOR FILM (value 0); needs the booth. */
    APP_ITEM_WEBCAM,       /**< Wi-Fi webcam (value 0); needs the booth. */
} app_item_kind_t;

/* ---- Photo booth ---- */
enum {
    APP_BOOTH_PHOTOS = 12,       /**< Stored photo slots. */
    APP_BOOTH_GIFS = 2,          /**< Stored GIF slots (APP_PHOTO_GIF_FRAMES each). */
    APP_CAMERA_GONE_S = 3,       /**< Booth page without a camera returns Home after this. */
};

typedef enum {
    APP_BOOTH_LIVE = 0,          /**< Viewfinder; look arrows, lens chip, GIF/SNAP/ROLL. */
    APP_BOOTH_COUNTDOWN,         /**< 3-2-1 before a photo or GIF. */
    APP_BOOTH_FLASH,             /**< White flash; the photo was requested. */
    APP_BOOTH_RECORD,            /**< GIF frames being requested. */
    APP_BOOTH_REVIEW,            /**< The capture (or why it failed) is shown. */
    APP_BOOTH_GALLERY,           /**< Browse and delete stored items. */
} app_booth_phase_t;

typedef enum {
    APP_BOOTH_RESULT_NONE = 0,   /**< Pending (saving). */
    APP_BOOTH_RESULT_SAVED,
    APP_BOOTH_RESULT_FULL,       /**< No free slot of that kind. */
    APP_BOOTH_RESULT_FAILED,     /**< Camera or storage error. */
    APP_BOOTH_RESULT_DELETED,
} app_booth_result_t;

/** Work the runtime must do; taken with app_game_take_booth_request(). */
typedef enum {
    APP_BOOTH_REQ_NONE = 0,
    APP_BOOTH_REQ_PHOTO,         /**< Save the current filtered frame as a photo. */
    APP_BOOTH_REQ_GIF_FRAME,     /**< Record GIF frame `frame`; the last one saves. */
    APP_BOOTH_REQ_DELETE,        /**< Delete gallery item `index` (0 = newest). */
} app_booth_request_kind_t;

typedef struct {
    app_booth_request_kind_t kind;
    uint8_t filter;              /**< app_photo_filter_t to bake in. */
    uint8_t res;                 /**< app_photo_res_t (GIFs are capped, see app_photo.h). */
    bool color;                  /**< Color film for this capture (look permitting). */
    uint8_t frame;               /**< GIF frame 0..APP_PHOTO_GIF_FRAMES-1. */
    uint8_t index;               /**< Gallery item for DELETE. */
} app_booth_request_t;

typedef struct {
    app_booth_phase_t phase;
    uint16_t phase_ticks;        /**< Ticks in the current phase. */
    bool gif;                    /**< The current/last capture is a GIF. */
    uint8_t count;               /**< Countdown digit, 3..1. */
    uint8_t frame;               /**< GIF frames requested so far. */
    uint8_t filter;              /**< Selected look; always owned. */
    uint8_t res;                 /**< Selected lens (app_photo_res_t); always owned. */
    bool color;                  /**< COLOR film selected (owned); B&W otherwise. GREEN and
                                      THERMAL looks always shoot B&W. */
    bool camera;                 /**< Runtime has a live camera. */
    uint8_t photos, gifs;        /**< Stored items, reported by the runtime. */
    uint8_t index;               /**< Gallery item shown, 0 = newest. */
    bool confirm_delete;         /**< DEL was tapped once; tap again to delete. */
    app_booth_result_t result;   /**< Last capture/delete outcome. */
    bool unplugged;              /**< On the booth page with the camera gone. */
    bool on_air;                 /**< Webcam streaming; the runtime serves the camera. */
    uint16_t cam_code;           /**< 1000..9999 viewers must send while on air; new each time. */
    uint8_t seq;                 /**< Bumped on every phase, picker or result change (wraps). */
} app_booth_snapshot_t;

/* ---- Spark Slots ---- */
enum {
    APP_SLOT_REELS = 3,
    APP_SLOT_STRIP = 16,     /**< Symbols per reel strip. */
    APP_SLOT_Q = 16,         /**< Reel position units per symbol. */
    APP_SLOTS_BET = 3,       /**< Sparks per spin. */
};

typedef enum {
    APP_SLOT_SEVEN = 0,
    APP_SLOT_STAR,
    APP_SLOT_EYE,
    APP_SLOT_BOLT,
    APP_SLOT_HEART,
    APP_SLOT_SHOE,
    APP_SLOT_SYMBOLS,
} app_slot_symbol_t;

typedef enum {
    APP_SLOTS_READY = 0,     /**< Waiting for a paid spin. */
    APP_SLOTS_SPINNING,      /**< Reels turning; STOP halts them left to right. */
    APP_SLOTS_RESULT,        /**< All reels stopped; payout shown. */
} app_slots_phase_t;

typedef struct {
    app_slots_phase_t phase;
    uint16_t pos_q[APP_SLOT_REELS];  /**< Reel position, 0..STRIP*Q-1. */
    uint8_t stopped;                 /**< Bit per stopped reel. */
    uint16_t payout;                 /**< Sparks won by the last spin. */
} app_slots_snapshot_t;

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
#define APP_OWN_GAME_BIT(i)  (1UL << (24 + (i)))
/* Looks and lenses use their own bytes: snapshot filters_owned / lenses_owned. */
#define APP_OWN_FILTER_BIT(i) (1U << (i))
#define APP_OWN_LENS_BIT(i) (1U << (i))
/* COLOR FILM shares the lens byte (lenses use bits 0..2). */
#define APP_OWN_FILM_BIT (1U << 7)
/* WEBCAM too (bit 6). */
#define APP_OWN_WEBCAM_BIT (1U << 6)

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
    APP_EVENT_SLOT_SPIN = 1U << 17,
    APP_EVENT_SLOT_STOP = 1U << 18,
    APP_EVENT_SLOT_DONE = 1U << 19,   /**< See snapshot slots.payout. */
    APP_EVENT_POWER = 1U << 20,       /**< snapshot.power changed. */
    APP_EVENT_FED = 1U << 21,         /**< Today's food (half the goal) was walked. */
    APP_EVENT_REVIVED = 1U << 22,     /**< A fainted pet was walked back to life. */
    APP_EVENT_DAY = 1U << 23,         /**< A day ended; health changed. */
    APP_EVENT_BLOCKED = 1U << 24,     /**< Fainted: snacks and games refused. */
    APP_EVENT_ARCADE = 1U << 25,      /**< A cabinet was opened or exited. */
    APP_EVENT_GRID_FLIP = 1U << 26,   /**< See snapshot grid.last_flip. */
    APP_EVENT_GRID_DONE = 1U << 27,   /**< Round over; see grid.result and payout. */
    APP_EVENT_WIFI_SETUP = 1U << 28,  /**< Runtime: start the Wi-Fi setup hotspot. */
    APP_EVENT_WIFI_STOP = 1U << 29,   /**< Runtime: stop the setup hotspot. */
    APP_EVENT_BIT_FLIP = 1U << 30,    /**< A switch moved; see bits.last_was_hit. */
    APP_EVENT_BIT_DONE = 1U << 31,    /**< Bit Flip run over; see bits.payout. */
};

/** Events that change spendable or configured data and deserve a prompt save. */
#define APP_EVENTS_SAVE_NOW (APP_EVENT_ATE | APP_EVENT_CARD_DEAL | APP_EVENT_CARD_REVEAL | \
                             APP_EVENT_CASH_OUT | APP_EVENT_SETTINGS | APP_EVENT_GOAL | \
                             APP_EVENT_BOUGHT | APP_EVENT_SLOT_DONE | APP_EVENT_FED | \
                             APP_EVENT_REVIVED | APP_EVENT_DAY | APP_EVENT_GRID_DONE | \
                             APP_EVENT_BIT_DONE)

typedef struct {
    const char *name;   /**< Upper-case label drawable by the pixel font. */
    uint16_t cost;      /**< Sparks. */
    uint8_t hunger;     /**< Temporary fullness added on top of walking, 0..100. */
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
    app_arcade_t arcade;        /**< Game open on the Arcade page, or LOBBY. */
    uint8_t store_page;         /**< 0..store pages-1. */
    app_power_t power;
    int8_t battery_pct;         /**< 0..100, or -1 when unknown. */
    bool battery_charging;
    app_mood_t mood;
    uint32_t owned;             /**< APP_OWN_*_BIT flags. */
    uint16_t filters_owned;     /**< APP_OWN_FILTER_BIT flags; NONE always set. */
    uint8_t lenses_owned;       /**< APP_OWN_LENS_BIT flags; LOW always set. */
    uint8_t anims_on;           /**< Bit per app_anim_t; only owned ones can be on. */
    app_sound_t sound;
    uint8_t settings_page;      /**< 0..1. */
    uint16_t sleep_s;           /**< Seconds without a touch before the screen sleeps. */
    uint8_t brightness_pct;     /**< Drawn brightness when awake, 60..100. */
    bool tap_steps;             /**< TAP STEPS: tapping the pet counts as a step. */
    bool module_lights;         /**< Interaction module mood LEDs on (Settings). */
    uint32_t tap_steps_today;   /**< Steps credited by pats today. */
    bool dev_mode;              /**< DEV MODE on (sandbox: infinite sparks, nothing kept). */
    uint8_t konami_pos;         /**< Konami inputs entered so far on the About page. */
    uint16_t pats;              /**< Lifetime pats (stops at APP_LOVE_PATS). */
    bool love_eyes;             /**< LOVE EYES unlocked: hearts when happy. */
    uint16_t credits_ticks;     /**< About page credits roll: ticks left. */
    bool dizzy;                 /**< Shaken: spiral eyes. */
    bool upside_down;           /**< Flipped: panicking. */
    app_tools_snapshot_t tools;
    uint8_t reset_hold;         /**< 0..100 progress of holding RESET PET. */
    uint32_t steps_total;
    uint32_t steps_today;
    uint32_t step_goal;
    uint32_t sparks;
    uint8_t hunger;             /**< Fullness 0..100: today's walking toward food_need plus snacks. */
    uint8_t happiness;          /**< 0..100. */
    uint32_t food_need;         /**< Steps that feed the pet today (goal / 2). */
    uint8_t hp;                 /**< Health hearts, 0..APP_HP_MAX. */
    bool fainted;
    bool fed_today;
    uint16_t fed_streak;        /**< Consecutive fed days ending yesterday. */
    uint32_t history[APP_HISTORY_DAYS];  /**< Steps per past day; [0] = yesterday. */
    int32_t day;                /**< Current calendar day key, 0 = clock unknown. */
    app_net_state_t net;
    uint32_t ipv4;              /**< Address in network byte order when ONLINE. */
    char setup_ap[APP_SETUP_AP_MAX];  /**< Hotspot to join while net is SETUP, else "". */
    bool camera_present;        /**< Camera module detected in the left slot (runtime). */
    uint16_t friendship;        /**< 0..999; hearts = friendship / 100. */
    app_pixel_style_t pixel_style;
    uint8_t color_index;        /**< 0..APP_COLOR_COUNT-1. */
    int8_t last_food;           /**< Food index of the last meal, -1 if none. */
    app_cards_snapshot_t cards;
    app_slots_snapshot_t slots;
    app_grid_snapshot_t grid;
    app_bits_snapshot_t bits;
    uint8_t arcade_row;         /**< Cabinet row shown in the arcade lobby. */
    app_booth_snapshot_t booth;
    app_says_snapshot_t says;
    app_stack_snapshot_t stack;
    uint8_t cue;                /**< app_cue_t of the newest cue... */
    uint8_t cue_seq;            /**< ...bumped (wraps) whenever a cue fires. */
    uint32_t save_seq;          /**< Increments whenever persistent data changes. */
} app_game_snapshot_t;

/** Persistent record: plain data, 88 bytes. Bump the save version on layout change;
 * fields are only ever appended so older records can be migrated by prefix. */
typedef struct {
    uint32_t steps_total;
    uint32_t steps_today;
    int32_t day;                /**< Days since epoch of steps_today, 0 if unknown. */
    uint32_t sparks;
    uint32_t owned;             /**< APP_OWN_*_BIT flags. */
    uint16_t spark_progress;    /**< Steps toward the next spark. */
    uint16_t friendship;
    uint8_t hunger;             /**< Snack fullness (walking is derived from steps_today). */
    uint8_t happiness;
    uint8_t pixel_style;
    uint8_t color_index;
    uint8_t best_streak;
    uint8_t goal_awarded;
    uint8_t anims_on;
    uint8_t goal_k;             /**< Daily goal in thousands of steps; 0 = default. */
    /* v3 */
    uint8_t hp;                 /**< 0..APP_HP_MAX. */
    uint8_t fainted;
    uint8_t fed_today;
    uint8_t sound;              /**< app_sound_t; was reserved (0 = ON), so no migration. */
    /* v4 */
    uint32_t history[APP_HISTORY_DAYS];
    uint16_t fed_streak;
    uint8_t sleep_code;         /**< 0 = default 30 s, else 1 + index into the sleep options. */
    uint8_t bright_code;        /**< 0 = default 100 %, else 1 + index into the brightness options. */
    /* v5 */
    uint16_t bits_best;         /**< Bit Flip best run (hits). */
    uint16_t reserved3;
    /* v6 */
    uint8_t filters_owned;      /**< APP_OWN_FILTER_BIT flags. */
    uint8_t filter;             /**< Selected photo look. */
    uint8_t lenses_owned;       /**< APP_OWN_LENS_BIT flags. */
    uint8_t lens;               /**< Selected photo resolution, | APP_RECORD_LENS_COLOR for color film. */
    /* v7 */
    uint8_t filters_owned_hi;   /**< Looks 8..15 (APP_OWN_FILTER_BIT >> 8). */
    uint8_t grid_level;         /**< Spark Grid level 1..APP_GRID_LEVELS; 0 = 1. */
    uint8_t says_best;          /**< Bitsy Says best rounds (was reserved, 0). */
    uint8_t stack_best;         /**< Spark Stack best rows (was reserved, 0). */
    /* v8 */
    uint8_t flags;              /**< APP_RECORD_FLAG_*. */
    uint8_t pats_lo, pats_hi;   /**< Lifetime pats, up to APP_LOVE_PATS (was reserved). */
    uint8_t eggs;               /**< APP_EGG_* found (was reserved). */
    /* v9 */
    uint16_t tools_xp;          /**< TOOLS XP. */
    uint8_t step_cal_pct;       /**< Step correction percent, 0 = 100. */
    uint8_t reserved6;
} app_game_record_t;

#define APP_EGG_LOVE 0x01U    /* LOVE EYES unlocked. */

#define APP_RECORD_FLAG_TAP_STEPS 0x01U   /* Tapping the pet counts as a step. */
#define APP_RECORD_FLAG_LIGHTS_OFF 0x02U  /* Interaction module mood LEDs off. */

/** Sizes of older records (prefixes of the current one), for save migration. */
#define APP_GAME_RECORD_V2_SIZE 32U
#define APP_GAME_RECORD_V3_SIZE 36U
#define APP_GAME_RECORD_V4_SIZE 68U
#define APP_GAME_RECORD_V5_SIZE 72U
#define APP_GAME_RECORD_V6_SIZE 76U
#define APP_GAME_RECORD_V7_SIZE 80U
#define APP_GAME_RECORD_V8_SIZE 84U
#define APP_RECORD_LENS_COLOR 0x80U   /* app_game_record_t.lens: color film selected. */

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
/** Steps the step sensor counted while the chip was in deep sleep (pocket
 *  mode): credited like sensor steps, STEP CAL included. */
void app_game_add_sensor_delta(app_game_handle_t handle, uint32_t delta);
/** Set the local calendar day (days since epoch); a new day resets steps_today.
 * Values <= 0 mean "clock unknown" and are ignored. */
void app_game_set_day(app_game_handle_t handle, int32_t day);
/** Report the battery: percent 0..100 (negative = unknown) and charging state. */
void app_game_set_battery(app_game_handle_t handle, int percent, bool charging);
/** Report network state and IPv4 address (network byte order) for display. */
void app_game_set_network(app_game_handle_t handle, app_net_state_t state, uint32_t ipv4);
/** Report the setup hotspot name shown while the state is APP_NET_SETUP
 * (copied, truncated to APP_SETUP_AP_MAX-1; NULL clears it). */
void app_game_set_setup_ap(app_game_handle_t handle, const char *name);
/** Report whether a camera module is in the left slot. It adds the booth page
 * and the booth's store items; it does not change what is owned. */
void app_game_set_camera(app_game_handle_t handle, bool present);
/** Report the photo booth's camera and stored item counts (every tick or on change). */
void app_game_set_booth(app_game_handle_t handle, bool camera, uint8_t photos, uint8_t gifs);
/** Take the booth's pending capture/delete request; false when there is none. */
bool app_game_take_booth_request(app_game_handle_t handle, app_booth_request_t *request);
/** Report how the last booth request ended. */
void app_game_booth_result(app_game_handle_t handle, app_booth_result_t result);
/** Wake the screen (button press, external event). No-op when awake. */
void app_game_wake(app_game_handle_t handle);
/** Put the pet to sleep now (screen off), like the menu's SLEEP. */
void app_game_sleep(app_game_handle_t handle);
/** Fire an app_cue_t from outside the model (the runtime's clock: New Year). */
void app_game_cue(app_game_handle_t handle, uint8_t cue);
/** Interaction module status from the runtime (TOOLS shows its tools when present). */
void app_game_set_module(app_game_handle_t handle, bool present);
/** GUARD MODE motion report from the runtime (counted while armed). */
void app_game_guard_motion(app_game_handle_t handle);
/** Interaction module key press (false = left, true = right): arcade controls. */
void app_game_module_key(app_game_handle_t handle, bool right);
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
/** Store items shown with or without a camera (booth items need one). */
int app_game_store_visible_count(bool camera);
/** Catalog index of the `position`-th shown item, or -1. */
int app_game_store_visible(bool camera, int position);
/** Catalog index selling this item, or -1 if it is free/not sold. */
int app_game_store_find(app_item_kind_t kind, uint8_t value);
/** Symbol shown on a reel at `row` (0 = pay line, +1 = next symbol up). */
uint8_t app_game_slot_symbol(int reel_pos_q, int row);
/** Sparks paid for three of `symbol` on the pay line. */
uint16_t app_game_slot_triple_pay(int symbol);

void app_game_export(app_game_handle_t handle, app_game_record_t *record);
/** Load a record; out-of-range fields are clamped. NULL arguments are no-ops. */
void app_game_import(app_game_handle_t handle, const app_game_record_t *record);

#ifdef __cplusplus
}
#endif
