// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The photo booth's gallery: up to APP_PHOTO_STORE_PHOTOS photos and
 * APP_PHOTO_STORE_GIFS GIFs. Device: NVS namespace "pp_photos" (one blob per
 * slot); Host simulator: RAM only. Thread-safe; contracts in ../README.md. */
#include <stdbool.h>
#include <stdint.h>
/* Relative: the Host simulator compiles without component include paths. */
#include "../../app_photo/include/app_photo.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_PHOTO_STORE_PHOTOS = 12,
    APP_PHOTO_STORE_GIFS = 2,
    APP_PHOTO_STORE_SLOTS = APP_PHOTO_STORE_PHOTOS + APP_PHOTO_STORE_GIFS,
};

typedef enum {
    APP_PHOTO_STORE_OK = 0,
    APP_PHOTO_STORE_ERR_ARG,
    APP_PHOTO_STORE_ERR_NOT_FOUND,
    APP_PHOTO_STORE_ERR_FULL,       /**< No free slot, or storage too full to stay safe. */
    APP_PHOTO_STORE_ERR_IO,         /**< Storage unavailable or a write failed. */
    APP_PHOTO_STORE_ERR_NO_MEM,
} app_photo_store_err_t;

typedef struct {
    uint32_t seq;      /**< Unique, increasing capture number; stable id for URLs. */
    uint8_t frames;    /**< 1 = photo, APP_PHOTO_GIF_FRAMES = GIF. */
    uint8_t filter;    /**< app_photo_filter_t. */
    uint8_t res;       /**< app_photo_res_t (lens only). */
    bool color;        /**< Color film. */
} app_photo_entry_t;

/** Load the index (device: reads every stored blob once). Idempotent. */
app_photo_store_err_t app_photo_store_init(void);
/** Entries newest first; returns how many were written (<= max). */
int app_photo_store_list(app_photo_entry_t *out, int max);
void app_photo_store_counts(uint8_t *photos, uint8_t *gifs);
/** Increments on every save or delete. */
uint32_t app_photo_store_revision(void);
/** Store a capture (header.frames picks the kind); assigns header.seq. */
app_photo_store_err_t app_photo_store_save(app_photo_t *photo);
/** Read an item by gallery position (0 = newest) or by seq. */
app_photo_store_err_t app_photo_store_load(int index, app_photo_t *out);
app_photo_store_err_t app_photo_store_load_seq(uint32_t seq, app_photo_t *out);
app_photo_store_err_t app_photo_store_delete(int index);

#ifdef __cplusplus
}
#endif
