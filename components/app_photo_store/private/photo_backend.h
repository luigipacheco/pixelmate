// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Storage behind the gallery index. Three backends share this table: files
 * (FAT on the "photos" partition, ESP-Mosaico), NVS (StackChan, StopWatch) and
 * RAM (Host simulator). The index calls every op with its lock held. */
#include <stdbool.h>
#include <stddef.h>
#include "../include/app_photo_store.h"

typedef enum {
    PHOTO_KIND_PHOTO = 0,   /* app_photo record, 1 frame. */
    PHOTO_KIND_GIF,         /* app_photo record, APP_PHOTO_GIF_FRAMES frames. */
} photo_kind_t;

/** One stored item as the index keeps it. */
typedef struct {
    uint32_t seq;
    uint32_t bytes;     /**< Record bytes. */
    uint8_t kind;       /**< photo_kind_t. */
    uint8_t frames;
    uint8_t filter;
    uint8_t res;        /**< Lens only. */
    bool color;
    uint16_t slot;      /**< The backend's own handle (NVS / RAM slot); unused on files. */
} photo_item_t;

typedef struct {
    const char *name;
    uint16_t max_items;   /**< Index capacity. */
    uint8_t photo_slots;  /**< Fixed photo / GIF slots; 0 = only space limits them. */
    uint8_t gif_slots;
    app_photo_store_err_t (*init)(void);
    /** Every valid item, any order (frees broken ones); *max_seq = largest seq
     * seen. -1 when the store could not be read (out of memory). */
    int (*scan)(photo_item_t *items, int max, uint32_t *max_seq);
    /** True when an item of `kind` taking `bytes` fits and the reserve stays free. */
    bool (*has_room)(uint8_t kind, size_t bytes);
    uint32_t (*free_bytes)(void);
    /** Store a record under item->seq; sets item->slot. Never leaves half a record. */
    app_photo_store_err_t (*write)(photo_item_t *item, const app_photo_t *photo, size_t len);
    app_photo_store_err_t (*read)(const photo_item_t *item, app_photo_t *out, size_t *len);
    app_photo_store_err_t (*erase)(const photo_item_t *item);
} photo_backend_t;

/** Pick and start the backend (device: and move old NVS photos to files).
 * Called once, by app_photo_store_init with the lock held. */
app_photo_store_err_t photo_backend_open(const photo_backend_t **out);
void photo_backend_lock(void);
void photo_backend_unlock(void);

/** Fill `out` from a stored record's header and size; false when it is not a
 * valid photo or GIF record (shared by the backends' scans). */
bool photo_item_from_header(const app_photo_header_t *h, size_t len, photo_item_t *out);

/* Device backends (not built for the Host simulator). */
extern const photo_backend_t photo_backend_nvs;
extern const photo_backend_t photo_backend_files;
/** True when the old "pp_photos" NVS namespace exists (something to move). */
bool photo_nvs_has_namespace(void);
/** True when the partition table has a "photos" FAT partition. */
bool photo_files_partition_exists(void);
/** Seqs at or below this stay taken on files (the move's largest NVS seq). */
void photo_files_seq_floor(uint32_t seq);
