# app_photo

PixelMate's photo format and image code, in pure C (no RTOS, hardware or
heap): lenses (resolutions), looks, palettes, the stored record, and PNG/GIF
encoders for the web gallery. Used by the runtime (both targets), the photo
store, the camera component and the web server.

| File | Responsibility |
| --- | --- |
| `app_photo.c` | Looks (luminance -> 4 shades), downscaling, palettes, 2 bpp packing |
| `app_photo_codec.c` | Indexed PNG (stored deflate) and looping GIF89a (LZW) |

```c
uint8_t small[64 * 64], shades[64 * 64];
app_photo_downscale(feed128, 128, small, 64);                 /* MID lens */
app_photo_filter(small, 64, APP_FILTER_HALFTONE, shades);
app_photo_t photo = {.header = {.filter = APP_FILTER_HALFTONE, .frames = 1,
                                .res = APP_PHOTO_RES_MID, .base = {217, 255, 0}}};
app_photo_pack(shades, 64 * 64, app_photo_frame(&photo, 0));

uint8_t *png = malloc(app_photo_png_bound(APP_PHOTO_RES_MID, 4));
size_t size = app_photo_encode_png(&photo, 4, png, app_photo_png_bound(APP_PHOTO_RES_MID, 4));   /* 256x256 */
```

## Lenses and looks

A photo is a lens (resolution) plus a look; any look works at any lens.

| Lens | Side | Frame bytes | Notes |
| --- | --- | --- | --- |
| LO RES | 32 | 256 | Free default: a very low-res camera |
| MID RES | 64 | 1024 | Store item |
| HI RES | 128 | 4096 | Store item; photos only (GIFs record at MID) |

All looks first stretch contrast to the frame's own 4th..96th luminance
percentile (THERMAL: 10th..90th), so a dim room still uses all four shades.

| Look | Effect |
| --- | --- |
| PLAIN | Four equal luminance bands (free) |
| DITHER | 4x4 ordered (Bayer) dithering between the bands |
| SCANLINES | Odd rows one shade darker |
| GAME-GREEN | PLAIN with four greens |
| THERMAL | Harder contrast, heat palette |
| EYE STICKERS | PLAIN plus two of the pet's eyes top-right (laid out at 64, resampled) |
| ASCII | 4x4-pixel character cells from the ramp ` .:-+x0@` by cell brightness; brighter cells also get a brighter ink |
| HALFTONE | Bright dots on a staggered grid (4 px cells, 8 px at HI), area by brightness, soft edges |

Palettes: shade 0 is darkest. The pet's color at 7/35/67/100 % for every look
except GAME-GREEN (fixed greens) and THERMAL (indigo/magenta/orange/yellow).

## Color film

`app_photo_filter_color()` turns a Y/U/V image into indices of
`app_photo_color_palette`, a fixed 16-color retro palette (index 0 black,
3 white), with ordered dithering (wider for DITHER). Luma gets half the B&W
contrast stretch and chroma a 1.5x boost. SCANLINES darkens odd rows, EYE
STICKERS uses white/black, ASCII and HALFTONE paint each cell in its own hue.
GAME-GREEN and THERMAL have no color version. Color frames are 4 bits per
pixel: 512 / 2048 / 8192 bytes at LO / MID / HI, so color GIFs (8 frames)
record at LO.

## Format

- Frames are square, 4 shades (2 bits per pixel), packed MSB-first, row-major.
- `app_photo_header_t` (12 bytes, little-endian `seq`) precedes the frames:
  magic `0x50`, version 3, look, frame count (1, or 8 for a GIF), the pet's
  color at capture (RGB), the lens (bit 7 = `APP_PHOTO_COLOR_FLAG`) and a
  capture sequence number. Version 2 photos (no color) stay readable.
  `app_photo_record_size()` gives 268 / 1036 / 4108 bytes for LO / MID / HI
  photos and 2060 / 8204 for LO / MID GIFs. `app_photo_t.data` holds 8 KB.
- `app_photo_header_valid()` rejects unknown versions, lenses, looks, frame
  counts and HI-res GIFs.

## Encoders

- `scale` multiplies the photo's side; the output side may be at most
  `APP_PHOTO_MAX_OUT` (512) pixels.
- `app_photo_encode_png()`: frame 0 as an indexed 2-bit PNG with a 4-entry
  palette; the zlib stream uses stored blocks (the data is tiny).
- `app_photo_encode_gif()`: every frame, `frame_ms` each, looping when there is
  more than one. Needs `APP_PHOTO_GIF_WORK_BYTES` (about 30 KB) of caller
  memory for the LZW hash table.
- Both write only to the caller's buffer and return 0 if it is too small;
  `*_bound()` gives a safe size. Real GIFs are far smaller than the bound.

## Threading

Every function is reentrant and keeps state only in caller memory. The looks
use a 512-byte histogram on the stack; call them from a task with a few KB of
stack.
