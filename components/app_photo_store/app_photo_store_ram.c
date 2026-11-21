// SPDX-License-Identifier: Apache-2.0
/* Host simulator backend: slots in RAM, lost when the simulator stops. The
 * simulator is single-threaded, so locking is a no-op. Not built for the device. */
#include <string.h>
#include "private/photo_backend.h"

static app_photo_t s_ram[APP_PHOTO_STORE_SLOTS];
static size_t s_len[APP_PHOTO_STORE_SLOTS];

app_photo_store_err_t photo_backend_init(void)
{
    return APP_PHOTO_STORE_OK;
}

void photo_backend_lock(void)
{
}

void photo_backend_unlock(void)
{
}

app_photo_store_err_t photo_backend_read(int slot, void *buf, size_t cap, size_t *len)
{
    if (!s_len[slot]) {
        return APP_PHOTO_STORE_ERR_NOT_FOUND;
    }
    if (s_len[slot] > cap) {
        return APP_PHOTO_STORE_ERR_IO;
    }
    memcpy(buf, &s_ram[slot], s_len[slot]);
    *len = s_len[slot];
    return APP_PHOTO_STORE_OK;
}

app_photo_store_err_t photo_backend_write(int slot, const void *data, size_t len)
{
    if (len > sizeof(s_ram[slot])) {
        return APP_PHOTO_STORE_ERR_ARG;
    }
    memcpy(&s_ram[slot], data, len);
    s_len[slot] = len;
    return APP_PHOTO_STORE_OK;
}

app_photo_store_err_t photo_backend_erase(int slot)
{
    s_len[slot] = 0;
    return APP_PHOTO_STORE_OK;
}

bool photo_backend_has_room(size_t len)
{
    (void)len;
    return true;
}
