// SPDX-License-Identifier: Apache-2.0
/* NVS backend (StackChan, StopWatch; ESP-Mosaico before it had a "photos"
 * partition): one blob per slot in the app's own namespace of the default
 * "nvs" partition, APP_PHOTO_STORE_PHOTOS photo slots then
 * APP_PHOTO_STORE_GIFS GIF slots. */
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
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
static nvs_handle_t s_nvs;
static bool s_open;
static bool s_used[APP_PHOTO_STORE_SLOTS];

static bool is_gif_slot(int slot)
{
    return slot >= APP_PHOTO_STORE_PHOTOS;
}

static void slot_key(int slot, char key[KEY_MAX])
{
    if (slot < APP_PHOTO_STORE_PHOTOS) {
        snprintf(key, KEY_MAX, "p%d", slot);
    } else {
        snprintf(key, KEY_MAX, "g%d", slot - APP_PHOTO_STORE_PHOTOS);
    }
}

bool photo_nvs_has_namespace(void)
{
    if (nvs_flash_init() != ESP_OK) {   /* No-op when already initialized. */
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {   /* Read-only: never creates it. */
        return false;
    }
    nvs_close(h);
    return true;
}

static app_photo_store_err_t nvs_init(void)
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

static app_photo_store_err_t read_slot(int slot, void *buf, size_t cap, size_t *len)
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

static app_photo_store_err_t erase_slot(int slot)
{
    char key[KEY_MAX];
    slot_key(slot, key);
    esp_err_t err = nvs_erase_key(s_nvs, key);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        s_used[slot] = false;
        return APP_PHOTO_STORE_OK;
    }
    return APP_PHOTO_STORE_ERR_IO;
}

static int nvs_scan(photo_item_t *items, int max, uint32_t *max_seq)
{
    app_photo_t *tmp = malloc(sizeof(*tmp));
    int n = 0;
    *max_seq = 0;
    if (!tmp) {
        return -1;
    }
    for (int s = 0; s < APP_PHOTO_STORE_SLOTS; ++s) {
        size_t len = 0;
        s_used[s] = false;
        if (read_slot(s, tmp, sizeof(*tmp), &len) != APP_PHOTO_STORE_OK) {
            continue;
        }
        photo_item_t it;
        const bool ok = photo_item_from_header(&tmp->header, len, &it)
                        && (it.kind == PHOTO_KIND_GIF) == is_gif_slot(s);
        if (!ok || n >= max) {
            erase_slot(s);   /* Corrupt or foreign: free the slot. */
            continue;
        }
        it.slot = (uint16_t)s;
        items[n++] = it;
        s_used[s] = true;
        if (it.seq > *max_seq) {
            *max_seq = it.seq;
        }
    }
    free(tmp);
    return n;
}

static int free_slot(uint8_t kind)
{
    const bool gif = kind == PHOTO_KIND_GIF;
    for (int s = gif ? APP_PHOTO_STORE_PHOTOS : 0; s < (gif ? APP_PHOTO_STORE_SLOTS : APP_PHOTO_STORE_PHOTOS); ++s) {
        if (!s_used[s]) {
            return s;
        }
    }
    return -1;
}

static size_t free_entries(void)
{
    nvs_stats_t stats;
    return nvs_get_stats(NULL, &stats) == ESP_OK ? stats.free_entries : 0;
}

static bool nvs_has_room(uint8_t kind, size_t bytes)
{
    const size_t needed = (bytes + ENTRY_BYTES - 1) / ENTRY_BYTES + 4 + RESERVE_ENTRIES;   /* + blob headers */
    return free_slot(kind) >= 0 && free_entries() >= needed;
}

static uint32_t nvs_free_bytes(void)
{
    const size_t entries = free_entries();
    return entries > RESERVE_ENTRIES ? (uint32_t)((entries - RESERVE_ENTRIES) * ENTRY_BYTES) : 0;
}

static app_photo_store_err_t nvs_write(photo_item_t *item, const app_photo_t *photo, size_t len)
{
    const int slot = free_slot(item->kind);
    if (slot < 0) {
        return APP_PHOTO_STORE_ERR_FULL;
    }
    char key[KEY_MAX];
    slot_key(slot, key);
    esp_err_t err = nvs_set_blob(s_nvs, key, photo, len);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s (%u B): %s", key, (unsigned)len, esp_err_to_name(err));
        nvs_erase_key(s_nvs, key);   /* Never leave a half-written slot behind. */
        return err == ESP_ERR_NVS_NOT_ENOUGH_SPACE ? APP_PHOTO_STORE_ERR_FULL : APP_PHOTO_STORE_ERR_IO;
    }
    s_used[slot] = true;
    item->slot = (uint16_t)slot;
    return APP_PHOTO_STORE_OK;
}

static app_photo_store_err_t nvs_read(const photo_item_t *item, app_photo_t *out, size_t *len)
{
    return read_slot(item->slot, out, sizeof(*out), len);
}

static app_photo_store_err_t nvs_erase(const photo_item_t *item)
{
    return erase_slot(item->slot);
}

const photo_backend_t photo_backend_nvs = {
    .name = "nvs",
    .max_items = APP_PHOTO_STORE_SLOTS,
    .photo_slots = APP_PHOTO_STORE_PHOTOS,
    .gif_slots = APP_PHOTO_STORE_GIFS,
    .init = nvs_init,
    .scan = nvs_scan,
    .has_room = nvs_has_room,
    .free_bytes = nvs_free_bytes,
    .write = nvs_write,
    .read = nvs_read,
    .erase = nvs_erase,
};
