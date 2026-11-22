// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Photo booth pixels, owned by the runtime: the live 64x64 feed (camera on the
 * device, a synthetic scene on the Host), the capture being recorded or
 * reviewed, and the gallery item on screen. Serves the model's booth requests
 * and reports results back. Game-loop only; instance-owned, no heap. */
#include <stdbool.h>
#include <stdint.h>
#include "../../app_game/include/app_game.h"

typedef enum {
    BOOTH_CAMERA_OFF = 0,     /**< Not needed right now (booth closed). */
    BOOTH_CAMERA_STARTING,
    BOOTH_CAMERA_LIVE,
    BOOTH_CAMERA_ABSENT,      /**< No camera module in the left slot. */
    BOOTH_CAMERA_ERROR,
} booth_camera_t;

typedef struct {
    booth_camera_t camera;
    bool present;                       /* Camera module detected (last probe). */
    bool host_present;                  /* Host simulator's pretend camera. */
    bool face_feed;                     /* Runtime: stream for the face finder too (set before update). */
    uint8_t yuv[3 * APP_PHOTO_MAX_PIXELS];   /* Latest feed frame: 128x128 Y, U, V planes. */
    uint32_t luma_seq;                  /* Changes with every new frame. */
    uint8_t live[APP_PHOTO_MAX_PIXELS]; /* Feed through lens, look and film: palette indices. */
    uint32_t live_seq;                  /* luma_seq that `live` was made from. */
    uint8_t live_filter, live_res;
    bool live_color;                    /* `live` holds color-film indices (0..15). */
    uint8_t work_yuv[3 * APP_PHOTO_MAX_PIXELS];  /* Feed scaled to a lens. */
    uint8_t work_shades[APP_PHOTO_MAX_PIXELS];   /* A capture's frame before packing. */
    app_photo_t capture;                /* Being recorded, then shown in REVIEW. */
    app_photo_t shown;                  /* Gallery item on screen. */
    int shown_index;                    /* Gallery index last loaded, -1 = reload. */
    bool shown_valid;                   /* `shown` holds that item. */
    uint32_t shown_revision;            /* Store revision when `shown` was loaded. */
    bool store_ready;
    uint32_t fake_t;                    /* Host synthetic scene clock. */
} booth_media_t;

void booth_media_init(booth_media_t *m);
/** After app_game_update: run the feed, serve requests, report status.
 * `base` is the pet color stored with captures. */
void booth_media_update(booth_media_t *m, app_game_handle_t game, app_photo_rgb_t base);
