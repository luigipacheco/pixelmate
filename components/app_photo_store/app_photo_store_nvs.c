// SPDX-License-Identifier: Apache-2.0
/* Device backend: one NVS blob per slot in the app's own namespace of the
 * default "nvs" partition. A mutex serializes the game loop and the web server. */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "private/photo_backend.h"

#define NAMESPACE "pp_photos"
enum {
    ENTRY_BYTES = 32,              /* NVS entry size. */
    KEY_MAX = 16,                  /* NVS key buffer (keys are "p0".."p11", "g0", "g1"). */
    /* Free space kept for the pet save, Wi-Fi and time zone after any photo
     * write (about two flash pages), so the gallery can never starve them. */
    RESERVE_ENTRIES = 256,
};

static const char *TAG = "photo_store";
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buffer;
static nvs_handle_t s_nvs;
static bool s_open;

static void slot_key(int slot, char key[KEY_MAX])
{
    if (slot < APP_PHOTO_STORE_PHOTOS) {
        snprintf(key, KEY_MAX, "p%d", slot);
    } else {
        snprintf(key, KEY_MAX, "g%d", slot - APP_PHOTO_STORE_PHOTOS);
    }
}

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

app_photo_store_err_t photo_backend_init(void)
{
    if (s_open) {
        return APP_PHOTO_STORE_OK;
    }
    const esp_err_t init = nvs_flash_init();   /* No-op when already initialized. */
    if (init != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable: %s", esp_err_to_name(init));
        return APP_PHOTO_STORE_ERR_IO;
    }
    const esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open %s: %s", NAMESPACE, esp_err_to_name(err));
        return APP_PHOTO_STORE_ERR_IO;
    }
    s_open = true;
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK) {
        ESP_LOGI(TAG, "nvs entries used %u free %u", (unsigned)stats.used_entries, (unsigned)stats.free_entries);
    }
    return APP_PHOTO_STORE_OK;
}

app_photo_store_err_t photo_backend_read(int slot, void *buf, size_t cap, size_t *len)
{
    char key[KEY_MAX];
    slot_key(slot, key);
    size_t size = cap;
    const esp_err_t err = nvs_get_blob(s_nvs, key, buf, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return APP_PHOTO_STORE_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read %s: %s", key, esp_err_to_name(err));
        return APP_PHOTO_STORE_ERR_IO;
    }
    *len = size;
    return APP_PHOTO_STORE_OK;
}

app_photo_store_err_t photo_backend_write(int slot, const void *data, size_t len)
{
    char key[KEY_MAX];
    slot_key(slot, key);
    esp_err_t err = nvs_set_blob(s_nvs, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s (%u B): %s", key, (unsigned)len, esp_err_to_name(err));
        nvs_erase_key(s_nvs, key);   /* Never leave a half-written slot behind. */
        return err == ESP_ERR_NVS_NOT_ENOUGH_SPACE ? APP_PHOTO_STORE_ERR_FULL : APP_PHOTO_STORE_ERR_IO;
    }
    return APP_PHOTO_STORE_OK;
}

app_photo_store_err_t photo_backend_erase(int slot)
{
    char key[KEY_MAX];
    slot_key(slot, key);
    esp_err_t err = nvs_erase_key(s_nvs, key);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND ? APP_PHOTO_STORE_OK : APP_PHOTO_STORE_ERR_IO;
}

bool photo_backend_has_room(size_t len)
{
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) != ESP_OK) {
        return false;
    }
    const size_t needed = (len + ENTRY_BYTES - 1) / ENTRY_BYTES + 4 + RESERVE_ENTRIES;   /* + blob headers */
    return stats.free_entries >= needed;
}
