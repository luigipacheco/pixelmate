// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Photo booth rules: filter picker, 3-2-1 countdown, flash, GIF recording,
 * review and gallery browsing. Pixels never reach the model: captures and
 * deletes leave as requests for the runtime, which reports results back. */
#include "../include/app_game.h"

typedef struct {
    app_booth_snapshot_t view;
    app_booth_request_t request;   /* Pending request, kind NONE when empty. */
} booth_t;

void booth_init(booth_t *b);
/** The booth page was shown: live viewfinder. */
void booth_enter(booth_t *b);
/** Left (swipe, panel, screen off, unplug): drop any countdown or recording. */
void booth_cancel(booth_t *b);
/** Tap inside the open booth; returns APP_EVENT_* bits. */
uint32_t booth_tap(booth_t *b, int32_t x, int32_t y, uint8_t filters_owned, uint8_t lenses_owned,
                   bool film_owned, bool fainted);
/** One 30 Hz tick; returns APP_EVENT_* bits. */
uint32_t booth_update(booth_t *b);
/** Runtime status: camera usable and stored item counts. */
void booth_set_status(booth_t *b, bool camera, uint8_t photos, uint8_t gifs);
/** Runtime finished the last capture or delete; returns APP_EVENT_* bits. */
uint32_t booth_result(booth_t *b, app_booth_result_t result);
/** Select a look or lens (caller checks ownership). */
void booth_select_filter(booth_t *b, uint8_t filter);
void booth_select_lens(booth_t *b, uint8_t res);
void booth_select_color(booth_t *b, bool color);
