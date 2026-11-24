// SPDX-License-Identifier: Apache-2.0
/* Files backend (ESP-Mosaico): ESP-IDF's FAT with wear levelling on the
 * "photos" partition, mounted at /photos (formatted on the first mount). One
 * folder, ROLL, with short 8.3 names:
 *   P0001234.PXP  an app_photo record (photo or GIF) with seq 1234
 * Every file is written under a temporary name (.TMP) and then renamed, so a
 * power cut never leaves half a file under a real name; the scan deletes
 * leftovers. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "private/photo_backend.h"

#define LABEL "photos"
#define MOUNT "/photos"
#define ROLL MOUNT "/ROLL"

enum {
    PATH_LEN = 40,
    NAME_LEN = 13,                   /* "P0001234.PXP" + NUL */
    RESERVE_BYTES = 64 * 1024,       /* Always left free, so FAT and wear levelling stay healthy. */
    CLUSTER = CONFIG_WL_SECTOR_SIZE, /* Allocation unit we format with. */
    SEQ_MAX = 9999999,               /* Seven digits in a name. */
    DOOMED_MAX = 16,                 /* Leftovers deleted per scan (the rest on the next boot). */
};

static const char *TAG = "photo_files";
static bool s_mounted;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static uint32_t s_seq_floor;

static void item_path(char path[PATH_LEN], char letter, uint32_t seq, const char *ext)
{
    snprintf(path, PATH_LEN, ROLL "/%c%07lu.%s", letter, (unsigned long)seq, ext);
}

bool photo_files_partition_exists(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, LABEL) != NULL;
}

void photo_files_seq_floor(uint32_t seq)
{
    if (seq > s_seq_floor) {
        s_seq_floor = seq;
    }
}

static uint32_t files_free_bytes(void)
{
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(MOUNT, &total, &free_bytes) != ESP_OK || free_bytes <= RESERVE_BYTES) {
        return 0;
    }
    free_bytes -= RESERVE_BYTES;
    return free_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)free_bytes;
}

static app_photo_store_err_t files_init(void)
{
    if (s_mounted) {
        return APP_PHOTO_STORE_OK;
    }
    const esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = CLUSTER,
    };
    const esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT, LABEL, &config, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount %s: %s", LABEL, esp_err_to_name(err));
        return APP_PHOTO_STORE_ERR_IO;
    }
    s_mounted = true;
    if (mkdir(ROLL, 0755) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir %s: errno %d", ROLL, errno);
        return APP_PHOTO_STORE_ERR_IO;
    }
    ESP_LOGI(TAG, "mounted %s, %lu KB free for photos", MOUNT, (unsigned long)(files_free_bytes() / 1024));
    return APP_PHOTO_STORE_OK;
}

/* "X0001234.EXT" -> letter, seq, ext (upper case). False for any other name. */
static bool parse_name(const char *name, char *letter, uint32_t *seq, char ext[4])
{
    if (strlen(name) != NAME_LEN - 1 || name[8] != '.') {
        return false;
    }
    uint32_t value = 0;
    for (int i = 1; i < 8; ++i) {
        if (!isdigit((unsigned char)name[i])) {
            return false;
        }
        value = value * 10U + (uint32_t)(name[i] - '0');
    }
    for (int i = 0; i < 3; ++i) {
        ext[i] = (char)toupper((unsigned char)name[9 + i]);
    }
    ext[3] = '\0';
    *letter = (char)toupper((unsigned char)name[0]);
    *seq = value;
    return true;
}

/* Read up to `cap` bytes of a file; ERR_IO when it is bigger than `cap`. */
static app_photo_store_err_t read_file(const char *path, void *buf, size_t cap, size_t *len)
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return errno == ENOENT ? APP_PHOTO_STORE_ERR_NOT_FOUND : APP_PHOTO_STORE_ERR_IO;
    }
    struct stat st;
    app_photo_store_err_t err = fstat(fd, &st) == 0 && (size_t)st.st_size <= cap ? APP_PHOTO_STORE_OK
                                : APP_PHOTO_STORE_ERR_IO;
    size_t done = 0;
    while (err == APP_PHOTO_STORE_OK && done < (size_t)st.st_size) {
        const ssize_t got = read(fd, (uint8_t *)buf + done, (size_t)st.st_size - done);
        if (got <= 0) {
            err = APP_PHOTO_STORE_ERR_IO;
        } else {
            done += (size_t)got;
        }
    }
    close(fd);
    *len = done;
    return err;
}

/* Write a whole new file; on failure nothing is left at `path`. */
static app_photo_store_err_t write_file(const char *path, const void *data, size_t len)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGW(TAG, "create %s: errno %d", path, errno);
        return APP_PHOTO_STORE_ERR_IO;
    }
    size_t done = 0;
    int error = 0;
    while (done < len) {
        const ssize_t put = write(fd, (const uint8_t *)data + done, len - done);
        if (put <= 0) {
            error = put < 0 ? errno : ENOSPC;
            break;
        }
        done += (size_t)put;
    }
    if (!error && fsync(fd) != 0) {
        error = errno;
    }
    if (close(fd) != 0 && !error) {
        error = errno;
    }
    if (!error) {
        return APP_PHOTO_STORE_OK;
    }
    ESP_LOGW(TAG, "write %s (%u B): errno %d", path, (unsigned)len, error);
    unlink(path);
    return error == ENOSPC ? APP_PHOTO_STORE_ERR_FULL : APP_PHOTO_STORE_ERR_IO;
}

/* A P-file's header and size as an item (its name's seq must match). */
static bool record_item(uint32_t seq, photo_item_t *out)
{
    char path[PATH_LEN];
    item_path(path, 'P', seq, "PXP");
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    app_photo_header_t h = {0};
    struct stat st = {0};
    const bool ok = read(fd, &h, sizeof(h)) == (ssize_t)sizeof(h) && fstat(fd, &st) == 0
                    && photo_item_from_header(&h, (size_t)st.st_size, out) && out->seq == seq;
    close(fd);
    return ok;
}

typedef struct {
    char names[DOOMED_MAX][NAME_LEN];
    int count;
} doomed_t;

static void doom(doomed_t *d, char letter, uint32_t seq, const char *ext)
{
    if (d->count < DOOMED_MAX) {
        snprintf(d->names[d->count++], NAME_LEN, "%c%07lu.%s", letter, (unsigned long)seq, ext);
    }
}

static int files_scan(photo_item_t *items, int max, uint32_t *max_seq)
{
    *max_seq = s_seq_floor;
    DIR *dir = opendir(ROLL);
    doomed_t *doomed = calloc(1, sizeof(*doomed));
    if (!dir || !doomed) {
        if (dir) {
            closedir(dir);
        }
        free(doomed);
        return -1;
    }
    int n = 0;
    const struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        char letter, ext[4];
        uint32_t seq;
        if (!parse_name(e->d_name, &letter, &seq, ext)) {
            continue;   /* Not ours: leave it alone. */
        }
        if (ext[0] == 'T' && ext[1] == 'M') {
            doom(doomed, letter, seq, ext);   /* A write cut short. */
            continue;
        }
        if (letter != 'P' || strcmp(ext, "PXP") != 0) {
            continue;   /* Another kind of file: leave it alone. */
        }
        if (seq > *max_seq) {
            *max_seq = seq;
        }
        photo_item_t it;
        if (!record_item(seq, &it)) {
            doom(doomed, letter, seq, ext);   /* Corrupt or foreign. */
        } else if (n < max) {
            items[n++] = it;
        }
    }
    closedir(dir);
    for (int i = 0; i < doomed->count; ++i) {
        char path[PATH_LEN];
        snprintf(path, sizeof(path), ROLL "/%s", doomed->names[i]);
        if (unlink(path) == 0) {
            ESP_LOGW(TAG, "removed leftover %s", doomed->names[i]);
        }
    }
    free(doomed);
    ESP_LOGI(TAG, "%d items, %lu KB free", n, (unsigned long)(files_free_bytes() / 1024));
    return n;
}

static bool files_has_room(uint8_t kind, size_t bytes)
{
    /* Whole clusters for the file, one more for folder growth. */
    const size_t need = (bytes + CLUSTER - 1) / CLUSTER * CLUSTER + CLUSTER;
    (void)kind;
    return files_free_bytes() >= need;
}

static app_photo_store_err_t files_write(photo_item_t *item, const app_photo_t *photo, size_t len)
{
    if (item->seq > SEQ_MAX) {
        return APP_PHOTO_STORE_ERR_FULL;
    }
    char tmp[PATH_LEN], path[PATH_LEN];
    item_path(tmp, 'P', item->seq, "TMP");
    item_path(path, 'P', item->seq, "PXP");
    app_photo_store_err_t err = write_file(tmp, photo, len);
    if (err == APP_PHOTO_STORE_OK && rename(tmp, path) != 0) {
        ESP_LOGW(TAG, "rename %s: errno %d", path, errno);
        unlink(tmp);
        err = APP_PHOTO_STORE_ERR_IO;
    }
    item->slot = 0;
    return err;
}

static app_photo_store_err_t files_read(const photo_item_t *item, app_photo_t *out, size_t *len)
{
    char path[PATH_LEN];
    item_path(path, 'P', item->seq, "PXP");
    return read_file(path, out, sizeof(*out), len);
}

static app_photo_store_err_t files_erase(const photo_item_t *item)
{
    char path[PATH_LEN];
    item_path(path, 'P', item->seq, "PXP");
    return unlink(path) == 0 || errno == ENOENT ? APP_PHOTO_STORE_OK : APP_PHOTO_STORE_ERR_IO;
}

const photo_backend_t photo_backend_files = {
    .name = "files",
    .max_items = APP_PHOTO_STORE_MAX_ITEMS,
    .photo_slots = 0,
    .gif_slots = 0,
    .init = files_init,
    .scan = files_scan,
    .has_room = files_has_room,
    .free_bytes = files_free_bytes,
    .write = files_write,
    .read = files_read,
    .erase = files_erase,
};
