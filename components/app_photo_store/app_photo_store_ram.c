// SPDX-License-Identifier: Apache-2.0
/* Host simulator backend: every item in RAM, lost when the simulator stops.
 * Behaves like the ESP-Mosaico files store (no fixed slots, the item cap)
 * with a 4 MB budget. The simulator is single-threaded, so locking is a
 * no-op. Not built for the device. */
#include <stdlib.h>
#include <string.h>
#include "private/photo_backend.h"

enum {
    BUDGET_BYTES = 4 * 1024 * 1024,
};

typedef struct {
    void *data;          /* The record; NULL = free slot. */
    size_t len;
} ram_slot_t;

static ram_slot_t s_slots[APP_PHOTO_STORE_MAX_ITEMS];
static size_t s_used_bytes;

void photo_backend_lock(void)
{
}

void photo_backend_unlock(void)
{
}

static app_photo_store_err_t ram_init(void)
{
    return APP_PHOTO_STORE_OK;
}

static int ram_scan(photo_item_t *items, int max, uint32_t *max_seq)
{
    (void)items;
    (void)max;
    *max_seq = 0;
    return 0;   /* Nothing survives a restart. */
}

static bool ram_has_room(uint8_t kind, size_t bytes)
{
    (void)kind;
    return s_used_bytes + bytes <= BUDGET_BYTES;
}

static uint32_t ram_free_bytes(void)
{
    return (uint32_t)(BUDGET_BYTES - s_used_bytes);
}

static int free_slot(void)
{
    for (int s = 0; s < APP_PHOTO_STORE_MAX_ITEMS; ++s) {
        if (!s_slots[s].data) {
            return s;
        }
    }
    return -1;
}

static app_photo_store_err_t ram_write(photo_item_t *item, const app_photo_t *photo, size_t len)
{
    const int s = free_slot();
    void *copy = s >= 0 ? malloc(len) : NULL;
    if (!copy) {
        return s < 0 ? APP_PHOTO_STORE_ERR_FULL : APP_PHOTO_STORE_ERR_NO_MEM;
    }
    memcpy(copy, photo, len);
    s_slots[s] = (ram_slot_t) {.data = copy, .len = len};
    s_used_bytes += len;
    item->slot = (uint16_t)s;
    return APP_PHOTO_STORE_OK;
}

static app_photo_store_err_t ram_read(const photo_item_t *item, app_photo_t *out, size_t *len)
{
    const ram_slot_t *slot = &s_slots[item->slot];
    if (!slot->data) {
        return APP_PHOTO_STORE_ERR_NOT_FOUND;
    }
    if (slot->len > sizeof(*out)) {
        return APP_PHOTO_STORE_ERR_IO;
    }
    memcpy(out, slot->data, slot->len);
    *len = slot->len;
    return APP_PHOTO_STORE_OK;
}

static app_photo_store_err_t ram_erase(const photo_item_t *item)
{
    ram_slot_t *slot = &s_slots[item->slot];
    s_used_bytes -= slot->len;
    free(slot->data);
    memset(slot, 0, sizeof(*slot));
    return APP_PHOTO_STORE_OK;
}

static const photo_backend_t s_ram_backend = {
    .name = "ram",
    .max_items = APP_PHOTO_STORE_MAX_ITEMS,
    .photo_slots = 0,
    .gif_slots = 0,
    .init = ram_init,
    .scan = ram_scan,
    .has_room = ram_has_room,
    .free_bytes = ram_free_bytes,
    .write = ram_write,
    .read = ram_read,
    .erase = ram_erase,
};

app_photo_store_err_t photo_backend_open(const photo_backend_t **out)
{
    *out = &s_ram_backend;
    return APP_PHOTO_STORE_OK;
}
