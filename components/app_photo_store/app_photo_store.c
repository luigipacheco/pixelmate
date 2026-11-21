// SPDX-License-Identifier: Apache-2.0
/* Gallery index over a slot backend: which slots hold what, newest first. */
#include <stdlib.h>
#include <string.h>
#include "include/app_photo_store.h"
#include "private/photo_backend.h"

typedef struct {
    bool used;
    uint32_t seq;
    uint8_t frames;
    uint8_t filter;
    uint8_t res;
    bool color;
} slot_t;

/* Single shared index (one gallery per device); guarded by the backend lock. */
static slot_t s_slots[APP_PHOTO_STORE_SLOTS];
static bool s_ready;
static uint32_t s_next_seq = 1;
static uint32_t s_revision;

static bool is_gif_slot(int slot)
{
    return slot >= APP_PHOTO_STORE_PHOTOS;
}

app_photo_store_err_t app_photo_store_init(void)
{
    photo_backend_lock();
    if (s_ready) {
        photo_backend_unlock();
        return APP_PHOTO_STORE_OK;
    }
    app_photo_store_err_t err = photo_backend_init();
    app_photo_t *tmp = err == APP_PHOTO_STORE_OK ? malloc(sizeof(*tmp)) : NULL;
    if (err == APP_PHOTO_STORE_OK && !tmp) {
        err = APP_PHOTO_STORE_ERR_NO_MEM;
    }
    for (int s = 0; err == APP_PHOTO_STORE_OK && s < APP_PHOTO_STORE_SLOTS; ++s) {
        size_t len = 0;
        s_slots[s].used = false;
        if (photo_backend_read(s, tmp, sizeof(*tmp), &len) != APP_PHOTO_STORE_OK) {
            continue;
        }
        const app_photo_header_t *h = &tmp->header;
        const bool kind_ok = is_gif_slot(s) ? h->frames == APP_PHOTO_GIF_FRAMES : h->frames == 1;
        if (!app_photo_header_valid(h) || !kind_ok || len != app_photo_record_size(h)) {
            photo_backend_erase(s);   /* Corrupt or foreign: free the slot. */
            continue;
        }
        s_slots[s] = (slot_t) {
            true, h->seq, h->frames, h->filter, (uint8_t)app_photo_lens(h), app_photo_is_color(h)
        };
        if (h->seq >= s_next_seq) {
            s_next_seq = h->seq + 1;
        }
    }
    free(tmp);
    s_ready = err == APP_PHOTO_STORE_OK;
    photo_backend_unlock();
    return err;
}

/* Used slots sorted newest first; returns the count. Lock held. */
static int sorted_slots(int order[APP_PHOTO_STORE_SLOTS])
{
    int n = 0;
    for (int s = 0; s < APP_PHOTO_STORE_SLOTS; ++s) {
        if (!s_slots[s].used) {
            continue;
        }
        int i = n++;
        while (i > 0 && s_slots[order[i - 1]].seq < s_slots[s].seq) {
            order[i] = order[i - 1];
            --i;
        }
        order[i] = s;
    }
    return n;
}

int app_photo_store_list(app_photo_entry_t *out, int max)
{
    int order[APP_PHOTO_STORE_SLOTS];
    photo_backend_lock();
    const int n = sorted_slots(order);
    int written = 0;
    for (int i = 0; out && i < n && written < max; ++i) {
        const slot_t *slot = &s_slots[order[i]];
        out[written++] = (app_photo_entry_t) {
            slot->seq, slot->frames, slot->filter, slot->res, slot->color
        };
    }
    photo_backend_unlock();
    return written;
}

void app_photo_store_counts(uint8_t *photos, uint8_t *gifs)
{
    uint8_t p = 0, g = 0;
    photo_backend_lock();
    for (int s = 0; s < APP_PHOTO_STORE_SLOTS; ++s) {
        if (s_slots[s].used) {
            ++*(is_gif_slot(s) ? &g : &p);
        }
    }
    photo_backend_unlock();
    if (photos) {
        *photos = p;
    }
    if (gifs) {
        *gifs = g;
    }
}

uint32_t app_photo_store_revision(void)
{
    photo_backend_lock();
    const uint32_t revision = s_revision;
    photo_backend_unlock();
    return revision;
}

app_photo_store_err_t app_photo_store_save(app_photo_t *photo)
{
    if (!photo) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    photo->header.magic = APP_PHOTO_MAGIC;
    photo->header.version = APP_PHOTO_VERSION;
    if (!app_photo_header_valid(&photo->header)) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    const bool gif = photo->header.frames > 1;
    const size_t len = app_photo_record_size(&photo->header);
    photo_backend_lock();
    if (!s_ready) {
        photo_backend_unlock();
        return APP_PHOTO_STORE_ERR_IO;
    }
    int slot = -1;
    for (int s = gif ? APP_PHOTO_STORE_PHOTOS : 0; s < (gif ? APP_PHOTO_STORE_SLOTS : APP_PHOTO_STORE_PHOTOS); ++s) {
        if (!s_slots[s].used) {
            slot = s;
            break;
        }
    }
    app_photo_store_err_t err = slot < 0 || !photo_backend_has_room(len) ? APP_PHOTO_STORE_ERR_FULL
                                : APP_PHOTO_STORE_OK;
    if (err == APP_PHOTO_STORE_OK) {
        photo->header.seq = s_next_seq;
        err = photo_backend_write(slot, photo, len);
    }
    if (err == APP_PHOTO_STORE_OK) {
        s_slots[slot] = (slot_t) {
            true, s_next_seq++, photo->header.frames, photo->header.filter,
            (uint8_t)app_photo_lens(&photo->header), app_photo_is_color(&photo->header)
        };
        ++s_revision;
    }
    photo_backend_unlock();
    return err;
}

static app_photo_store_err_t read_slot(int slot, app_photo_t *out)
{
    size_t len = 0;
    const app_photo_store_err_t err = photo_backend_read(slot, out, sizeof(*out), &len);
    if (err != APP_PHOTO_STORE_OK) {
        return err;
    }
    return app_photo_header_valid(&out->header) && len == app_photo_record_size(&out->header)
           ? APP_PHOTO_STORE_OK : APP_PHOTO_STORE_ERR_IO;
}

/* Slot of gallery position `index`, or -1. Lock held. */
static int slot_at(int index)
{
    int order[APP_PHOTO_STORE_SLOTS];
    const int n = sorted_slots(order);
    return index >= 0 && index < n ? order[index] : -1;
}

app_photo_store_err_t app_photo_store_load(int index, app_photo_t *out)
{
    if (!out) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    photo_backend_lock();
    const int slot = slot_at(index);
    const app_photo_store_err_t err = slot < 0 ? APP_PHOTO_STORE_ERR_NOT_FOUND : read_slot(slot, out);
    photo_backend_unlock();
    return err;
}

app_photo_store_err_t app_photo_store_load_seq(uint32_t seq, app_photo_t *out)
{
    if (!out) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    photo_backend_lock();
    app_photo_store_err_t err = APP_PHOTO_STORE_ERR_NOT_FOUND;
    for (int s = 0; s < APP_PHOTO_STORE_SLOTS; ++s) {
        if (s_slots[s].used && s_slots[s].seq == seq) {
            err = read_slot(s, out);
            break;
        }
    }
    photo_backend_unlock();
    return err;
}

app_photo_store_err_t app_photo_store_delete(int index)
{
    photo_backend_lock();
    const int slot = slot_at(index);
    app_photo_store_err_t err = slot < 0 ? APP_PHOTO_STORE_ERR_NOT_FOUND : photo_backend_erase(slot);
    if (err == APP_PHOTO_STORE_OK) {
        s_slots[slot].used = false;
        ++s_revision;
    }
    photo_backend_unlock();
    return err;
}
