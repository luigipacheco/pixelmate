// SPDX-License-Identifier: Apache-2.0
/* Gallery index over a backend: what is stored, newest first. */
#include <stdlib.h>
#include <string.h>
#include "include/app_photo_store.h"
#include "private/photo_backend.h"

/* Single shared index (one gallery per device); guarded by the backend lock.
 * s_items is sorted newest (largest seq) first, so a position is an index. */
static const photo_backend_t *s_be;
static photo_item_t *s_items;
static int s_count;
static int s_max;
static bool s_ready;
static uint32_t s_next_seq = 1;
static uint32_t s_revision;

bool photo_item_from_header(const app_photo_header_t *h, size_t len, photo_item_t *out)
{
    if (!app_photo_header_valid(h) || len != app_photo_record_size(h)) {
        return false;
    }
    *out = (photo_item_t) {
        .seq = h->seq,
        .bytes = (uint32_t)len,
        .kind = h->frames > 1 ? PHOTO_KIND_GIF : PHOTO_KIND_PHOTO,
        .frames = h->frames,
        .filter = h->filter,
        .res = (uint8_t)app_photo_lens(h),
        .color = app_photo_is_color(h),
    };
    return true;
}

static int newest_first(const void *a, const void *b)
{
    const uint32_t sa = ((const photo_item_t *)a)->seq, sb = ((const photo_item_t *)b)->seq;
    return sa < sb ? 1 : (sa > sb ? -1 : 0);
}

app_photo_store_err_t app_photo_store_init(void)
{
    photo_backend_lock();
    if (s_ready) {
        photo_backend_unlock();
        return APP_PHOTO_STORE_OK;
    }
    app_photo_store_err_t err = photo_backend_open(&s_be);
    if (err == APP_PHOTO_STORE_OK && !s_items) {
        s_max = s_be->max_items;
        s_items = calloc((size_t)s_max, sizeof(*s_items));
        err = s_items ? APP_PHOTO_STORE_OK : APP_PHOTO_STORE_ERR_NO_MEM;
    }
    if (err == APP_PHOTO_STORE_OK) {
        uint32_t max_seq = 0;
        s_count = s_be->scan(s_items, s_max, &max_seq);
        err = s_count < 0 ? APP_PHOTO_STORE_ERR_NO_MEM : APP_PHOTO_STORE_OK;
        s_count = s_count < 0 ? 0 : s_count;
        qsort(s_items, (size_t)s_count, sizeof(*s_items), newest_first);
        if (s_count > 0 && s_items[0].seq > max_seq) {
            max_seq = s_items[0].seq;
        }
        s_next_seq = max_seq + 1;
    }
    s_ready = err == APP_PHOTO_STORE_OK;
    photo_backend_unlock();
    return err;
}

int app_photo_store_list(app_photo_entry_t *out, int max)
{
    photo_backend_lock();
    int written = 0;
    for (int i = 0; out && i < s_count && written < max; ++i) {
        const photo_item_t *it = &s_items[i];
        out[written++] = (app_photo_entry_t) {
            .seq = it->seq, .frames = it->frames, .filter = it->filter, .res = it->res, .color = it->color,
        };
    }
    photo_backend_unlock();
    return written;
}

/* Item at gallery position `index`, or NULL. Lock held. */
static photo_item_t *item_at(int index)
{
    return index >= 0 && index < s_count ? &s_items[index] : NULL;
}

/* Item with this seq, or NULL. Lock held. */
static photo_item_t *item_seq(uint32_t seq)
{
    for (int i = 0; i < s_count; ++i) {
        if (s_items[i].seq == seq) {
            return &s_items[i];
        }
    }
    return NULL;
}

/* Add an item in seq order. Lock held; the caller checked s_count < s_max. */
static void insert_item(const photo_item_t *item)
{
    int i = 0;
    while (i < s_count && s_items[i].seq > item->seq) {
        ++i;
    }
    memmove(&s_items[i + 1], &s_items[i], (size_t)(s_count - i) * sizeof(*s_items));
    s_items[i] = *item;
    ++s_count;
    ++s_revision;
}

void app_photo_store_usage(app_photo_store_usage_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    photo_backend_lock();
    for (int i = 0; i < s_count; ++i) {
        ++*(s_items[i].kind == PHOTO_KIND_GIF ? &out->gifs : &out->photos);
    }
    if (s_ready) {
        out->max_items = (uint16_t)s_max;
        out->max_photos = s_be->photo_slots;
        out->max_gifs = s_be->gif_slots;
        out->free_bytes = s_be->free_bytes();
    }
    photo_backend_unlock();
}

bool app_photo_store_fits(const app_photo_header_t *header)
{
    if (!header) {
        return false;
    }
    app_photo_header_t h = *header;
    h.magic = APP_PHOTO_MAGIC;
    h.version = APP_PHOTO_VERSION;
    photo_item_t item;
    if (!photo_item_from_header(&h, app_photo_record_size(&h), &item)) {
        return false;
    }
    photo_backend_lock();
    const bool fits = s_ready && s_count < s_max && s_be->has_room(item.kind, item.bytes);
    photo_backend_unlock();
    return fits;
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
    const size_t len = app_photo_record_size(&photo->header);
    photo_backend_lock();
    if (!s_ready) {
        photo_backend_unlock();
        return APP_PHOTO_STORE_ERR_IO;
    }
    photo->header.seq = s_next_seq;
    photo_item_t item;
    photo_item_from_header(&photo->header, len, &item);
    app_photo_store_err_t err = s_count >= s_max || !s_be->has_room(item.kind, len) ? APP_PHOTO_STORE_ERR_FULL
                                : s_be->write(&item, photo, len);
    if (err == APP_PHOTO_STORE_OK) {
        ++s_next_seq;
        insert_item(&item);
    }
    photo_backend_unlock();
    return err;
}

static app_photo_store_err_t read_item(const photo_item_t *it, app_photo_t *out)
{
    size_t len = 0;
    const app_photo_store_err_t err = s_be->read(it, out, &len);
    if (err != APP_PHOTO_STORE_OK) {
        return err;
    }
    return app_photo_header_valid(&out->header) && len == app_photo_record_size(&out->header)
           ? APP_PHOTO_STORE_OK : APP_PHOTO_STORE_ERR_IO;
}

app_photo_store_err_t app_photo_store_load(int index, app_photo_t *out)
{
    if (!out) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    photo_backend_lock();
    const photo_item_t *it = item_at(index);
    const app_photo_store_err_t err = it ? read_item(it, out) : APP_PHOTO_STORE_ERR_NOT_FOUND;
    photo_backend_unlock();
    return err;
}

app_photo_store_err_t app_photo_store_load_seq(uint32_t seq, app_photo_t *out)
{
    if (!out) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    photo_backend_lock();
    const photo_item_t *it = item_seq(seq);
    const app_photo_store_err_t err = it ? read_item(it, out) : APP_PHOTO_STORE_ERR_NOT_FOUND;
    photo_backend_unlock();
    return err;
}

app_photo_store_err_t app_photo_store_delete(int index)
{
    photo_backend_lock();
    photo_item_t *it = item_at(index);
    const app_photo_store_err_t err = it ? s_be->erase(it) : APP_PHOTO_STORE_ERR_NOT_FOUND;
    if (err == APP_PHOTO_STORE_OK) {
        memmove(it, it + 1, (size_t)(s_count - index - 1) * sizeof(*s_items));
        --s_count;
        ++s_revision;
    }
    photo_backend_unlock();
    return err;
}
