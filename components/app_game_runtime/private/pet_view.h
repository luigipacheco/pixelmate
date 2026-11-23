// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Screen composition and one-shot animations (transitions, particles, floaters,
 * card flips, food toss). Reads model snapshots and event bits; never writes
 * the model. Instance-owned by the runtime; update at 30 Hz, draw per frame. */
#include <stdint.h>
#include "booth_media.h"
#include "pet_eyes.h"
#include "pixel_draw.h"
#include "../../app_game/include/app_game.h"

enum { VIEW_PARTICLES = 56, VIEW_FLOATERS = 6, VIEW_FLOATER_TEXT = 16 };

/* ---- TOOLS data, fed by the runtime while a tool is open ---- */
enum { PET_TOOL_HIST = 96, PET_TOOL_APS = 8 };

typedef struct {
    float acc[3];      /* g */
    float gyr[3];      /* degrees/s */
    float mag[3];      /* raw magnetometer counts */
    float mv, ma;      /* battery */
    float light;       /* interaction module light level, 0..100 */
    bool pir;          /* interaction module motion */
} pet_tool_sample_t;

typedef struct {
    char ssid[20];
    int8_t rssi;
    bool open;
} pet_tool_ap_t;

typedef struct {
    pet_tool_sample_t hist[PET_TOOL_HIST];
    int head, count;
    bool has_mag;
    float mag_min[3], mag_max[3];   /* Hard-iron calibration (compass). */
    float mag_base, metal_dev;      /* Metal detector: slow baseline and deviation. */
    float zero_x, zero_y;           /* LEVEL zero. */
    uint8_t zero_seq_seen;
    pet_tool_ap_t aps[PET_TOOL_APS];
    int ap_count;                   /* -1 while scanning. */
    bool ir_flash;
} pet_tools_view_t;

typedef struct {
    float x, y, vx, vy, gravity;
    uint16_t life, max_life;
    uint8_t kind;
    Color color;
} view_particle_t;

typedef struct {
    char text[VIEW_FLOATER_TEXT];
    float x, y;
    uint16_t life;
    Color color;
} view_floater_t;

typedef struct {
    pet_eyes_t eyes;
    view_particle_t particles[VIEW_PARTICLES];
    view_floater_t floaters[VIEW_FLOATERS];
    uint8_t particle_cursor, floater_cursor;
    app_screen_t screen, prev_screen;
    float trans;              /* 0..1 screen transition progress (1 = settled). */
    int trans_dir;
    app_overlay_t overlay_drawn;  /* Panel currently on screen (may be retracting). */
    float overlay_pos;        /* 0 = hidden above, 1 = fully down. */
    float anchor_x, anchor_y, anchor_s;  /* Eased eye placement. */
    uint16_t dance_ticks;
    float grid_flip_t;        /* 0..1 flip of the grid tile just turned. */
    uint32_t grid_flipped;    /* Tiles the player flipped this round. */
    float bits_flip_t;        /* 0..1 lever animation of the switches in bits_flip_mask. */
    uint8_t bits_flip_mask;
    uint8_t prev_bits_switches;
    float bits_pop_t;         /* 0..1 pop of a new Bit Flip target. */
    float arcade_scroll;      /* Eased arcade row (0 = first row). */
    uint8_t seen_cue_seq;     /* Last arcade/dev cue animated. */
    uint8_t seen_act_seq;     /* Last act step animated. */
    pet_tools_view_t tools;
    float crt_t;              /* 0..1 CRT power-on when a cabinet opens, >= 1 idle. */
    uint8_t prev_slot_stopped;  /* Reel-stop bits seen last tick. */
    uint8_t prev_hp;            /* Health seen last tick, for day-change floaters. */
    float steps_shown, sparks_shown, ring_shown;
    uint32_t prev_sparks;
    uint16_t spark_pulse, spark_flash;
    float food_t;             /* 0..1 food toss progress, >= 1 idle. */
    int8_t food_index;
    float flip_t, deal_t, slide_t;  /* Card animations, >= 1 idle. */
    uint8_t prev_booth_phase, prev_booth_count, prev_booth_result, prev_booth_seq;
    bool prev_booth_air;
    const booth_media_t *media;   /* Booth pixels, owned by the runtime; set after init. */
    uint32_t t, rng;
    bool primed;
} pet_view_t;

void pet_view_init(pet_view_t *view);
void pet_view_update(pet_view_t *view, const app_game_snapshot_t *snapshot, uint32_t events);
void pet_view_draw(const pet_view_t *view, const app_game_snapshot_t *snapshot);

/** TOOLS: append one sensor sample (and update compass/metal/level state). */
void pet_view_tool_sample(pet_view_t *view, const app_game_snapshot_t *snapshot, const pet_tool_sample_t *sample,
                          bool has_mag);
/** TOOLS: Wi-Fi scan results (count -1 = scanning). */
void pet_view_tool_aps(pet_view_t *view, const pet_tool_ap_t *aps, int count);

/** Local time for clock mode. */
typedef struct {
    int hour, minute, second, wday, mday, month;
} pet_clock_t;

/** Clock mode frame (sleeping on USB power with the time known). */
void pet_view_draw_clock(const pet_view_t *view, const app_game_snapshot_t *snapshot, const pet_clock_t *clock);
