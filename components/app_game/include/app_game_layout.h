// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Screen geometry shared by the model's hit tests and the view's drawing, so a
 * button is always drawn where it is tapped. Units are pixels on the 480x480 panel. */
#include <stdbool.h>
#include <stdint.h>

/* Round screens (boards with pixelmate_feature(ROUND_SCREEN), e.g. the M5
 * StopWatch): the glass is a circle of radius APP_ROUND_R around the view's
 * centre, so corners are not visible. Set once by app_game_create() from
 * app_game_config_t.round_screen; layout functions below pick round positions
 * from it. Square boards keep the original layout. */
extern bool app_layout_round;
enum { APP_ROUND_R = 233 };

typedef struct {
    int16_t x, y, w, h;
} app_rect_t;

static inline bool app_rect_hit(app_rect_t r, int32_t px, int32_t py)
{
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

enum {
    APP_VIEW_W = 480,
    APP_VIEW_H = 480,
    APP_TOP_BAR_H = 56,
    APP_DOTS_Y = 452,
    APP_SWIPE_MIN_PX = 70,    /**< Horizontal travel that turns a drag into a swipe. */
    APP_TAP_SLOP_PX = 28,     /**< Movement still accepted as a tap. */
    APP_PULL_ZONE_H = 90,     /**< A downward drag starting above this opens the menu. */
    APP_PULL_MIN_PX = 70,     /**< Vertical travel that opens/closes a panel. */
    APP_MENU_H = 362,         /**< Height of the pull-down menu panel. */
};

/* Eye placement: center and scale relative to the home eyes (0 = hidden). */
typedef struct {
    int16_t cx, cy;
    float scale;
} app_eye_anchor_t;

static inline app_eye_anchor_t app_eye_anchor_table(int screen, int overlay, int arcade)
{
    switch (overlay) {
    case 1: return (app_eye_anchor_t) {240, 412, 0.4f};    /* menu: peeking below */
    case 2: return (app_eye_anchor_t) {240, 160, 0.72f};   /* style preview */
    case 3: return (app_eye_anchor_t) {240, 140, 0.0f};    /* store: hidden */
    case 4: return (app_eye_anchor_t) {240, 400, 0.42f};   /* goal: watching the number */
    case 5: return (app_eye_anchor_t) {240, 140, 0.0f};    /* wifi: hidden */
    case 6: return (app_eye_anchor_t) {240, 140, 0.0f};    /* settings: hidden */
    case 7: return (app_eye_anchor_t) {240, 360, 0.2f};   /* about: peeking at the credits */
    default: break;
    }
    switch (screen) {
    case 0: return (app_eye_anchor_t) {240, 236, 1.0f};    /* home */
    case 2: return (app_eye_anchor_t) {240, 150, 0.62f};   /* feed */
    case 4: return app_layout_round ? (app_eye_anchor_t) {370, 108, 0.2f}    /* tools: a lab assistant */
                                    : (app_eye_anchor_t) {420, 92, 0.22f};   /* ...in the corner */
    case 5: return app_layout_round ? (app_eye_anchor_t) {370, 108, 0.2f}    /* booth: peeking */
                                    : (app_eye_anchor_t) {416, 92, 0.22f};   /* over the viewfinder */
    case 3:                                                 /* arcade */
        switch (arcade) {
        /* app_arcade_t order: slots, stack, grid, bit flip, says, HI-LO. */
        case 1: return (app_eye_anchor_t) {240, 98, 0.42f};    /* slots */
        case 2: return app_layout_round ? (app_eye_anchor_t) {370, 104, 0.22f}   /* stack: peeking */
                                        : (app_eye_anchor_t) {420, 92, 0.24f};   /* at the tower */
        case 3: return app_layout_round ? (app_eye_anchor_t) {416, 248, 0.22f}   /* grid: watching */
                                        : (app_eye_anchor_t) {426, 340, 0.26f};  /* from the side */
        case 4: return (app_eye_anchor_t) {240, 96, 0.4f};     /* bit flip */
        case 5: return (app_eye_anchor_t) {240, 104, 0.5f};    /* says: the eyes show the colors */
        case 6: return (app_eye_anchor_t) {240, 104, 0.46f};   /* HI-LO */
        default: return (app_eye_anchor_t) {240, 98, 0.42f};   /* lobby: watching */
        }
    default: return (app_eye_anchor_t) {240, 206, 0.0f};   /* steps: no eyes */
    }
}

/* Where the eyes sit and how big. Round screens: in the arcade (lobby and the
 * games with eyes on top) the eyes shrink and drop a little so they clear the
 * step and spark counters, which sit lower there; Bit Flip's go to the side of
 * its TARGET; on TOOLS the corner they used is outside the glass and no other
 * spot is free in every tool, so they hide. */
static inline app_eye_anchor_t app_eye_anchor(int screen, int overlay, int arcade)
{
    app_eye_anchor_t a = app_eye_anchor_table(screen, overlay, arcade);
    if (app_layout_round && overlay == 0) {
        if (screen == 3 && arcade == 4) {
            a = (app_eye_anchor_t) {382, 120, 0.18f};
        } else if (screen == 3 && arcade != 2 && arcade != 3) {
            a.cy += 10.0f;
            a.scale *= 0.75f;
        } else if (screen == 4) {
            a = (app_eye_anchor_t) {240, 414, 0.0f};
        }
    }
    return a;
}

/* Arcade lobby: three cabinets side by side. */
static inline app_rect_t app_arcade_cabinet_rect(int i)
{
    if (app_layout_round) {   /* Shorter, inside the circle. */
        return (app_rect_t) {(int16_t)(52 + i * 128), 140, 120, 224};
    }
    return (app_rect_t) {(int16_t)(16 + i * 152), 152, 144, 280};
}
/* Leaves a game, back to the cabinets (left of the eyes, under the top bar). */
static inline app_rect_t app_arcade_exit_rect(void)
{
    return app_layout_round ? (app_rect_t) {76, 92, 100, 40}
                            : (app_rect_t) {12, 72, 104, 44};
}

/* Clock mode on Home: small eyes on top, the time under them. */
static inline app_eye_anchor_t app_clock_eye_anchor(void)
{
    return (app_eye_anchor_t) {240, 124, 0.5f};
}
enum {
    APP_CLOCK_TIME_Y = 214,      /**< Top of the time digits. */
    APP_CLOCK_TIME_SCALE = 13,   /**< Pixels per font dot (digits are 5x7 dots). */
    APP_CLOCK_DATE_Y = 340,      /**< Top of the date line. */
    APP_CLOCK_DATE_SCALE = 4,
};

static inline app_rect_t app_home_eyes_rect(void)
{
    return (app_rect_t) {80, 120, 320, 240};
}

static inline app_rect_t app_feed_item_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(58 + i * 124), 252, 116, 132};
    }
    return (app_rect_t) {(int16_t)(24 + i * 148), 266, 136, 160};
}

static inline app_rect_t app_cards_deal_rect(void)
{
    return app_layout_round ? (app_rect_t) {140, 344, 200, 56}
                            : (app_rect_t) {130, 362, 220, 64};
}
static inline app_rect_t app_cards_hi_rect(void)
{
    return app_layout_round ? (app_rect_t) {66, 344, 114, 56}
                            : (app_rect_t) {24, 362, 140, 64};
}
static inline app_rect_t app_cards_cash_rect(void)
{
    return app_layout_round ? (app_rect_t) {186, 344, 108, 56}
                            : (app_rect_t) {176, 362, 128, 64};
}
static inline app_rect_t app_cards_lo_rect(void)
{
    return app_layout_round ? (app_rect_t) {300, 344, 114, 56}
                            : (app_rect_t) {316, 362, 140, 64};
}
static inline app_rect_t app_cards_slot_rect(int slot)
{
    return (app_rect_t) {(int16_t)(slot ? 256 : 112), 176, 112, 160};
}

/* Spark Slots: three reel windows and the SPIN/STOP button. */
enum { APP_SLOT_WINDOW_W = 112, APP_SLOT_WINDOW_H = 160, APP_SLOT_SYMBOL_H = 64 };
static inline app_rect_t app_slots_window_rect(int reel)
{
    return (app_rect_t) {(int16_t)(56 + reel * 128), 160, APP_SLOT_WINDOW_W, APP_SLOT_WINDOW_H};
}
static inline app_rect_t app_slots_reels_rect(void)
{
    return (app_rect_t) {48, 152, 384, 176};
}
static inline app_rect_t app_slots_button_rect(void)
{
    return (app_rect_t) {130, 372, 220, 64};
}

/* Spark Grid: 5x5 tiles, a hint column/row, and a PLAY/CASH button. */
#define APP_GRID_CELL (app_layout_round ? 42 : 52)
#define APP_GRID_X (app_layout_round ? 104 : 52)
#define APP_GRID_Y (app_layout_round ? 138 : 126)
static inline app_rect_t app_grid_tile_rect(int tile)
{
    return (app_rect_t) {(int16_t)(APP_GRID_X + (tile % 5) * APP_GRID_CELL),
                         (int16_t)(APP_GRID_Y + (tile / 5) * APP_GRID_CELL), APP_GRID_CELL, APP_GRID_CELL};
}
static inline app_rect_t app_grid_button_rect(void)
{
    return app_layout_round ? (app_rect_t) {150, 402, 180, 40}
                            : (app_rect_t) {382, 126, 88, 64};
}

/* Bit Flip: 8 switches (128 on the left) and the PLAY button. */
static inline app_rect_t app_bits_switch_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(42 + i * 50), 262, 46, 84};
    }
    return (app_rect_t) {(int16_t)(16 + i * 56), 262, 52, 92};
}
static inline app_rect_t app_bits_button_rect(void)
{
    return (app_rect_t) {130, 384, 220, 56};
}

/* Photo booth: the photo fills a 256 px square (32/64/128 px lenses at 8/4/2x),
 * arrows either side (looks, or items in the gallery), a lens chip top-left and
 * three buttons: GIF | SNAP | ROLL, or BACK | - | DEL. */
static inline app_rect_t app_booth_lens_rect(void)
{
    return app_layout_round ? (app_rect_t) {56, 132, 96, 40}
                            : (app_rect_t) {12, 70, 110, 44};
}
/* Film chip under the lens chip: B&W or COLOR (once COLOR FILM is owned). */
/* Webcam ON AIR chip, right of the label, above the next-look arrow. */
static inline app_rect_t app_booth_air_rect(void)
{
    return app_layout_round ? (app_rect_t) {336, 132, 88, 40}
                            : (app_rect_t) {376, 122, 92, 46};
}
static inline app_rect_t app_booth_film_rect(void)
{
    return app_layout_round ? (app_rect_t) {56, 180, 80, 40}
                            : (app_rect_t) {12, 122, 84, 46};
}
static inline app_rect_t app_booth_view_rect(void)
{
    return app_layout_round ? (app_rect_t) {144, 132, 192, 192}
                            : (app_rect_t) {112, 120, 256, 256};
}
static inline app_rect_t app_booth_prev_rect(void)
{
    return app_layout_round ? (app_rect_t) {40, 228, 96, 96}
                            : (app_rect_t) {8, 176, 100, 144};
}
static inline app_rect_t app_booth_next_rect(void)
{
    return app_layout_round ? (app_rect_t) {344, 228, 96, 96}
                            : (app_rect_t) {372, 176, 100, 144};
}
static inline app_rect_t app_booth_button_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(76 + i * 112), 344, 104, 48};
    }
    return (app_rect_t) {(int16_t)(16 + i * 152), 388, 144, 56};
}
/* Locked booth: the teaser's button to the store. */
static inline app_rect_t app_booth_unlock_rect(void)
{
    return (app_rect_t) {110, 352, 260, 64};
}

/* Bitsy Says: four pads in a 2x2 block under the eyes; PLAY sits on top
 * of them while waiting. */
static inline app_rect_t app_says_pad_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(76 + (i % 2) * 168), (int16_t)(160 + (i / 2) * 112), 160, 104};
    }
    return (app_rect_t) {(int16_t)(48 + (i % 2) * 196), (int16_t)(176 + (i / 2) * 128), 188, 120};
}
static inline app_rect_t app_says_button_rect(void)
{
    return (app_rect_t) {140, 270, 200, 64};
}
/* Spark Stack: a 12x12 cell tower; tap anywhere below the top bar to drop. */
#define APP_STACK_CELL (app_layout_round ? 26 : 30)
#define APP_STACK_X (app_layout_round ? 84 : 60)
#define APP_STACK_BOTTOM (app_layout_round ? 394 : 438)
#define APP_STACK_ROW_H (app_layout_round ? 22 : 24)
static inline app_rect_t app_stack_button_rect(void)
{
    return (app_rect_t) {140, 250, 200, 64};
}

/* TOOLS: a 3x3 grid of tools under the level bar; inside a tool, BACK sits
 * top-left and up to three buttons along the bottom. */
static inline app_rect_t app_tools_cell_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(64 + (i % 3) * 120), (int16_t)(124 + (i / 3) * 88), 112, 80};
    }
    return (app_rect_t) {(int16_t)(20 + (i % 3) * 148), (int16_t)(126 + (i / 3) * 102), 140, 94};
}
static inline app_rect_t app_tools_back_rect(void)
{
    return app_layout_round ? (app_rect_t) {76, 92, 100, 40}
                            : (app_rect_t) {12, 64, 104, 42};
}
static inline app_rect_t app_tools_button_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(76 + i * 112), 352, 104, 48};
    }
    return (app_rect_t) {(int16_t)(16 + i * 152), 388, 144, 52};
}
/* IR remote: a 2x3 grid of buttons. */
static inline app_rect_t app_tools_ir_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(60 + (i % 2) * 184), (int16_t)(126 + (i / 2) * 78), 176, 66};
    }
    return (app_rect_t) {(int16_t)(40 + (i % 2) * 210), (int16_t)(130 + (i / 2) * 84), 190, 70};
}

/* Pull-down menu entries (inside the APP_MENU_H panel). The top row holds the
 * battery readout (left), SLEEP (center) and the spark counter (right). */
static inline app_rect_t app_menu_sleep_rect(void)
{
    /* Round: the narrow top of the glass holds SLEEP alone. */
    return app_layout_round ? (app_rect_t) {190, 12, 100, 38} : (app_rect_t) {176, 8, 128, 44};
}
static inline app_rect_t app_menu_store_rect(void)
{
    return app_layout_round ? (app_rect_t) {64, 88, 352, 62} : (app_rect_t) {40, 64, 400, 66};
}
static inline app_rect_t app_menu_style_rect(void)
{
    return app_layout_round ? (app_rect_t) {64, 160, 352, 62} : (app_rect_t) {40, 142, 400, 66};
}
static inline app_rect_t app_menu_settings_rect(void)
{
    return app_layout_round ? (app_rect_t) {64, 232, 352, 62} : (app_rect_t) {40, 220, 400, 66};
}

/* Network line under the menu buttons: opens the Wi-Fi panel. */
static inline app_rect_t app_menu_wifi_rect(void)
{
    return app_layout_round ? (app_rect_t) {64, 300, 352, 28} : (app_rect_t) {40, 290, 400, 30};
}

/* Wi-Fi panel: one action button (SET UP / STOP / CHANGE WIFI). */
static inline app_rect_t app_wifi_action_rect(void)
{
    return app_layout_round ? (app_rect_t) {120, 360, 240, 56} : (app_rect_t) {100, 376, 280, 64};
}

/* Daily goal panel. */
static inline app_rect_t app_goal_minus_rect(void)
{
    return (app_rect_t) {40, 222, 150, 84};
}
static inline app_rect_t app_goal_plus_rect(void)
{
    return (app_rect_t) {290, 222, 150, 84};
}

/* Shared by the style and store panels. */
static inline app_rect_t app_panel_back_rect(void)
{
    /* Round: under the title, on the first band wide enough for it. */
    return app_layout_round ? (app_rect_t) {100, 50, 104, 36} : (app_rect_t) {12, 8, 116, 44};
}

/* Y of panel titles and of the HUD counters (steps, sparks). Round: the title
 * alone in the narrow top, the counters on the wider band under it. */
static inline int app_panel_title_y(void)
{
    return app_layout_round ? 18 : 20;
}
static inline int app_hud_y(void)
{
    return app_layout_round ? 56 : 18;
}
/* Left and right edges for HUD items. */
static inline int app_hud_left(void)
{
    return app_layout_round ? 104 : 18;
}
static inline int app_hud_right(void)
{
    return app_layout_round ? 376 : 462;
}

static inline app_rect_t app_style_pixel_rect(int i)
{
    if (app_layout_round) {
        return (app_rect_t) {(int16_t)(56 + i * 128), 248, 120, 48};
    }
    return (app_rect_t) {(int16_t)(24 + i * 148), 284, 136, 52};
}
static inline app_rect_t app_style_color_rect(int i)
{
    if (app_layout_round) {   /* Two rows of three: the bottom of the circle is narrow. */
        return (app_rect_t) {(int16_t)(132 + (i % 3) * 76), (int16_t)(322 + (i / 3) * 62), 64, 52};
    }
    return (app_rect_t) {(int16_t)(24 + i * 74), 372, 64, 56};
}
/* Y of the style panel's PIXELS and COLOR labels. */
static inline int app_style_label_y(int which)
{
    return app_layout_round ? (which ? 302 : 226) : (which ? 350 : 262);
}
static inline app_rect_t app_store_row_rect(int row)
{
    /* Round: three rows in the wide middle band of the circle. */
    return app_layout_round ? (app_rect_t) {56, (int16_t)(100 + row * 80), 368, 74}
                            : (app_rect_t) {16, (int16_t)(68 + row * 92), 448, 84};
}

/* Settings: same row geometry as the store, 4 rows per page (3 round). */
#define APP_SETTINGS_PER_PAGE (app_layout_round ? 3 : 4)
static inline app_rect_t app_settings_row_rect(int row)
{
    return app_store_row_rect(row);
}
