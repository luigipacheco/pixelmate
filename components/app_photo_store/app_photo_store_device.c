// SPDX-License-Identifier: Apache-2.0
/* Device side of the store: the lock, the backend choice (files when the
 * partition table has a "photos" partition, else NVS) and the one-time move of
 * NVS photos to files. A mutex serializes the game loop and the web server. */
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "private/photo_backend.h"

static const char *TAG = "photo_store";
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buffer;

void photo_backend_lock(void)
{
    /* Created on first use from the game loop (app_photo_store_init runs
     * before the web server can call in), so creation needs no lock. */
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buffer);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void photo_backend_unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* Copy every NVS photo / GIF to files under its own seq, then erase its NVS
 * key. Only the "pp_photos" namespace is touched. A power cut at any point is
 * safe: a key is erased only after its file is complete, and a file that is
 * already there (moved before the cut) is not written again. */
static void move_nvs_photos(void)
{
    if (!photo_nvs_has_namespace()) {
        return;   /* Nothing was ever stored in NVS (checked without creating it). */
    }
    const photo_backend_t *from = &photo_backend_nvs, *to = &photo_backend_files;
    photo_item_t *items = malloc(from->max_items * sizeof(*items));
    app_photo_t *record = malloc(APP_PHOTO_SMALL_RECORD_MAX);   /* NVS records are version 3. */
    uint32_t max_seq = 0;
    const int n = items && record && from->init() == APP_PHOTO_STORE_OK
                  ? from->scan(items, from->max_items, &max_seq) : -1;
    /* New captures must never reuse an NVS seq, even one that failed to move. */
    photo_files_seq_floor(max_seq);
    int moved = 0;
    for (int i = 0; i < n; ++i) {
        photo_item_t item = items[i];
        size_t len = 0;
        app_photo_store_err_t err = to->read(&item, record, APP_PHOTO_SMALL_RECORD_MAX, &len);
        if (err != APP_PHOTO_STORE_OK) {
            err = from->read(&items[i], record, APP_PHOTO_SMALL_RECORD_MAX, &len);
            if (err == APP_PHOTO_STORE_OK) {
                err = to->write(&item, record, len);
            }
        }
        if (err == APP_PHOTO_STORE_OK) {
            err = from->erase(&items[i]);
        }
        if (err == APP_PHOTO_STORE_OK) {
            ++moved;
        } else {
            ESP_LOGW(TAG, "move seq %lu to files: %d (kept in NVS)", (unsigned long)items[i].seq, (int)err);
        }
    }
    if (n > 0) {
        ESP_LOGI(TAG, "moved %d of %d NVS photos to files", moved, n);
    }
    free(record);
    free(items);
}

app_photo_store_err_t photo_backend_open(const photo_backend_t **out)
{
    if (photo_files_partition_exists()) {
        if (photo_backend_files.init() == APP_PHOTO_STORE_OK) {
            move_nvs_photos();
            *out = &photo_backend_files;
            return APP_PHOTO_STORE_OK;
        }
        ESP_LOGW(TAG, "photos partition unusable, keeping photos in NVS");
    }
    *out = &photo_backend_nvs;
    return photo_backend_nvs.init();
}
