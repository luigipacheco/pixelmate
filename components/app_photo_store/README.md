# app_photo_store

The photo booth's gallery: pixel photos and GIFs (`app_photo` records, any
lens; GIFs are LO or MID). One API, three backends:

| Where | Backend | Limits |
| --- | --- | --- |
| ESP-Mosaico (`photos` partition) | files: FAT + wear levelling, `app_photo_store_files.c` | space (4 MB, 64 KB kept free) and at most `APP_PHOTO_STORE_MAX_ITEMS` (300) items |
| StackChan, StopWatch (no `photos` partition) | NVS namespace `pp_photos`, `app_photo_store_nvs.c` | 12 photo slots, 2 GIF slots, NVS space |
| Host simulator | RAM, `app_photo_store_ram.c` (listed only in `game.sim.json`) | 4 MB, 300 items, lost on exit |

The device picks at boot: files when the partition table has a `photos` FAT
partition and it mounts, otherwise NVS (`app_photo_store_device.c`).

```c
if (app_photo_store_init() == APP_PHOTO_STORE_OK) {
    if (app_photo_store_fits(&photo.header)) { /* would it be saved now? */
        app_photo_store_save(&photo);          /* assigns photo.header.seq */
    }
    app_photo_entry_t list[APP_PHOTO_STORE_MAX_ITEMS];   /* big: heap or PSRAM in a task */
    int n = app_photo_store_list(list, APP_PHOTO_STORE_MAX_ITEMS);   /* newest first */
    app_photo_store_load_seq(list[0].seq, &photo);
    app_photo_store_delete(0);                 /* gallery position 0 = newest */
}
```

## Rules

- `header.frames` picks the kind: 1 = photo, 8 = GIF. Positions count both
  kinds, newest first.
- `seq` continues from the largest stored one (ids stay unique across
  reboots; deleting the newest items and rebooting reuses their ids) and
  names the item in web URLs, never positions.
- `ERR_FULL` ("ROLL IS FULL") when the item does not fit: no free slot of the
  kind (NVS), the item cap (300), or not enough space while keeping the
  reserve. `app_photo_store_fits(&header)` asks the same question before a
  capture, for the exact record size of that header (frames, lens, color
  film). `app_photo_store_usage()` gives counts per kind, fixed slots (0 =
  none, only space and the cap), the cap and free bytes; `revision` is bumped
  on every save and delete.
- `init` reads every stored header once and frees items whose header, kind or
  size is wrong.

### Files (ESP-Mosaico)

- `esp_vfs_fat_spiflash_mount_rw_wl` mounts the `photos` partition
  (0xc00000, 4 MB, after `main_app`) at `/photos`, formatting it on the first
  mount (4 KB clusters); no data image is flashed. Items live in
  `/photos/ROLL` with 8.3 names: `P0001234.PXP` is an `app_photo` record
  exactly as NVS stored it, seq 1234.
- Every file is written under a temporary name (`.TMP`), synced, then
  renamed. `init` deletes leftovers (temporary files and broken records; up to
  16 per boot) and leaves other names alone.
- A file takes whole 4 KB clusters, so a LO photo costs 4 KB and a HI color
  photo or a MID GIF 12 KB: about 1000 LO photos or 320 HI color photos
  would fit, so the 300-item cap usually comes first; a roll heavy in GIFs
  and color fills by space.
- **Moving the old NVS photos.** On a boot with the files backend, if the NVS
  namespace `pp_photos` exists (checked read-only, so it is never created),
  every valid photo / GIF there is copied to a `P` file with its own seq, and
  only then is its NVS key erased. A file already there (power cut after the
  copy) is not written again, its key is just erased. A record that fails
  stays in NVS for the next boot, and new seqs start above every NVS seq, so
  it can never collide. Only `pp_photos` is touched: never the pet save, Wi-Fi,
  time zone or Vibe Mode records, nor the SPI NAND.

### NVS (StackChan, StopWatch)

- Slots `p0`..`p11` and `g0`, `g1`, one blob each in the app's own namespace
  `pp_photos` of the default `nvs` partition.
- Storage budget: a photo takes 268 / 1036 / 4108 bytes (LO / MID / HI), a GIF
  2060 / 8204 (LO / MID). Twelve HI photos and two MID GIFs would need about
  66 KB, more than the 64 KB `nvs` partition, so space, not slots, is the real
  limit at HI RES. A save is refused unless 256 NVS entries (8 KB) stay free
  afterwards, so photos can never starve the pet save, Wi-Fi or time zone
  records.
- A failed write erases the slot's key, so no half-written record remains.

## Threading

One gallery per device. Every call takes a mutex, so the game loop (captures,
deletes) and the HTTP server (listing, encoding) can use it at once; reads and
writes happen under the lock (a GIF write blocks readers for its flash time,
about 0.1 s). Call `app_photo_store_init()` from the game loop before the web
server starts using the store (on the first Mosaico boot it formats the
partition and moves the NVS photos, a second or two). Not for ISRs.
