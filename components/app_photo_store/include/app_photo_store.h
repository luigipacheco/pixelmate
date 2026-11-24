// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The photo booth's gallery: pixel photos and GIFs. ESP-Mosaico: files on the
 * "photos" FAT partition, limited by space and APP_PHOTO_STORE_MAX_ITEMS;
 * StackChan / StopWatch: NVS namespace "pp_photos", APP_PHOTO_STORE_PHOTOS +
 * APP_PHOTO_STORE_GIFS slots; Host simulator: RAM. Thread-safe; contracts in
 * ../README.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Relative: the Host simulator compiles without component include paths. */
#include "../../app_photo/include/app_photo.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_PHOTO_STORE_PHOTOS = 12,       /**< Photo slots of the NVS store. */
    APP_PHOTO_STORE_GIFS = 2,          /**< GIF slots of the NVS store. */
    APP_PHOTO_STORE_SLOTS = APP_PHOTO_STORE_PHOTOS + APP_PHOTO_STORE_GIFS,
    APP_PHOTO_STORE_MAX_ITEMS = 300,   /**< Most items any store keeps (size list buffers with it). */
};

typedef enum {
    APP_PHOTO_STORE_OK = 0,
    APP_PHOTO_STORE_ERR_ARG,
    APP_PHOTO_STORE_ERR_NOT_FOUND,
    APP_PHOTO_STORE_ERR_FULL,       /**< No free slot, the item cap, or storage too full to stay safe. */
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

typedef struct {
    uint16_t photos, gifs;            /**< Items stored of each kind. */
    uint16_t max_items;               /**< Cap on all items together (0 before init). */
    uint16_t max_photos, max_gifs;    /**< Fixed slots (NVS); 0 = only space and the cap limit them. */
    uint32_t free_bytes;              /**< Room left for new items, after the safety reserve. */
    uint32_t max_record;              /**< Largest record it keeps: size load buffers with it (0 before init). */
    bool big_records;                 /**< Version 4 records too (files, RAM): GIFs at every lens,
                                           up to HI. False on NVS: version 3 limits only. */
} app_photo_store_usage_t;

/** Mount / load the index (device: reads every stored header once; the first
 * boot with a "photos" partition also moves NVS photos there). Idempotent. */
app_photo_store_err_t app_photo_store_init(void);
/** Entries newest first; returns how many were written (<= max). */
int app_photo_store_list(app_photo_entry_t *out, int max);
/** Counts per kind, the limits and free space. */
void app_photo_store_usage(app_photo_store_usage_t *out);
/** True when a capture with this header (frames, lens, color film) would be
 * saved now: a record this store keeps (see big_records), a free slot of its
 * kind, under the item cap, and room for its size with the reserve kept.
 * False before init or for a bad header. */
bool app_photo_store_fits(const app_photo_header_t *header);
/** Increments on every save or delete. */
uint32_t app_photo_store_revision(void);
/** Store a capture (header.frames picks the kind); assigns header.seq and
 * the version (3 when the record follows version 3 rules, else 4). ERR_ARG
 * for a record this store does not keep (version 4 on NVS). */
app_photo_store_err_t app_photo_store_save(app_photo_t *photo);
/** Read an item by gallery position (0 = newest) or by seq into `out`, which
 * holds `cap` bytes (usage max_record fits any item; ERR_NO_MEM if too small). */
app_photo_store_err_t app_photo_store_load(int index, app_photo_t *out, size_t cap);
app_photo_store_err_t app_photo_store_load_seq(uint32_t seq, app_photo_t *out, size_t cap);
app_photo_store_err_t app_photo_store_delete(int index);

#ifdef __cplusplus
}
#endif
