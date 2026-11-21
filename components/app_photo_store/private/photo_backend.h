// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Storage behind the gallery index: NVS on the device, RAM on the Host.
 * Slot s is a photo for s < APP_PHOTO_STORE_PHOTOS, a GIF otherwise.
 * The index calls these with its lock held (except lock/unlock themselves). */
#include <stdbool.h>
#include <stddef.h>
#include "../include/app_photo_store.h"

app_photo_store_err_t photo_backend_init(void);
void photo_backend_lock(void);
void photo_backend_unlock(void);
/** Read slot `slot` into buf; *len = stored size. NOT_FOUND when empty. */
app_photo_store_err_t photo_backend_read(int slot, void *buf, size_t cap, size_t *len);
app_photo_store_err_t photo_backend_write(int slot, const void *data, size_t len);
app_photo_store_err_t photo_backend_erase(int slot);
/** True when `len` more bytes fit while leaving room for the pet's own saves. */
bool photo_backend_has_room(size_t len);
