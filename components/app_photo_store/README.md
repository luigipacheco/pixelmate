# app_photo_store

The photo booth's gallery: 12 photo slots and 2 GIF slots of `app_photo`
records (any lens; GIFs are LO or MID). On the device each slot is one NVS blob in the app's own namespace
`pp_photos` of the default `nvs` partition (`p0`..`p11`, `g0`, `g1`); the Host
simulator keeps them in RAM (`app_photo_store_ram.c`, listed only in
`game.sim.json`).

```c
if (app_photo_store_init() == APP_PHOTO_STORE_OK) {
    app_photo_store_save(&photo);              /* assigns photo.header.seq */
    app_photo_entry_t list[APP_PHOTO_STORE_SLOTS];
    int n = app_photo_store_list(list, APP_PHOTO_STORE_SLOTS);   /* newest first */
    app_photo_store_load_seq(list[0].seq, &photo);
    app_photo_store_delete(0);                 /* gallery position 0 = newest */
}
```

## Rules

- `header.frames` picks the slot kind: 1 = photo, 8 = GIF. A full kind returns
  `APP_PHOTO_STORE_ERR_FULL`.
- Storage budget: a photo takes 268 / 1036 / 4108 bytes (LO / MID / HI), a GIF
  2060 / 8204 (LO / MID). Twelve HI photos and two MID GIFs would need about
  66 KB, more than the 64 KB `nvs` partition, so space, not slots, is the real
  limit at HI RES. A save is refused (`ERR_FULL`, "ROLL IS FULL") unless 256
  NVS entries (8 KB) stay free afterwards, so photos can never starve the pet
  save, Wi-Fi or time zone records. Partitions are not changed and the SPI
  NAND (Vibe Mode downloads) is never touched.
- `init` reads every slot once (about 28 KB of reads) and frees slots whose
  header, kind or size is wrong. `seq` continues from the largest stored one, so
  ids stay unique across reboots; web URLs use `seq`, not positions.
- A failed write erases the slot's key, so no half-written record remains.

## Threading

One gallery per device. Every call takes a mutex, so the game loop (captures,
deletes) and the HTTP server (listing, encoding) can use it at once; reads and
writes happen under the lock (a GIF write blocks readers for its flash time).
Call `app_photo_store_init()` from the game loop before the web server starts
using the store. Not for ISRs.
