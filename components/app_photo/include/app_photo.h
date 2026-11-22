// SPDX-License-Identifier: Apache-2.0
#pragma once
/* PixelMate photos: square four-shade images at three resolutions ("lenses"),
 * the looks that make them from a camera's luminance, their palettes, and
 * PNG/GIF encoders. Pure C (no RTOS, no hardware); every function is
 * reentrant. Contracts: ../README.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lenses: the photo's resolution. LOW is free; MID and HI are store items. */
typedef enum {
    APP_PHOTO_RES_LOW = 0,     /**< 32x32: a very low-res camera. */
    APP_PHOTO_RES_MID,         /**< 64x64. */
    APP_PHOTO_RES_HIGH,        /**< 128x128 (photos only; GIFs record at MID). */
    APP_PHOTO_RES_COUNT,
} app_photo_res_t;

enum {
    APP_PHOTO_MIN_SIDE = 32,
    APP_PHOTO_MAX_SIDE = 128,                       /**< Also the camera feed's size. */
    APP_PHOTO_MAX_PIXELS = APP_PHOTO_MAX_SIDE * APP_PHOTO_MAX_SIDE,
    APP_PHOTO_SHADES = 4,                           /**< B&W film: 2 bits per pixel. */
    APP_PHOTO_COLORS = 16,                          /**< Color film: 4 bits per pixel. */
    APP_PHOTO_GIF_FRAMES = 8,
    APP_PHOTO_GIF_MAX_RES = APP_PHOTO_RES_MID,      /**< B&W GIFs above this record at it. */
    APP_PHOTO_GIF_MAX_RES_COLOR = APP_PHOTO_RES_LOW,/**< Color GIFs above this record at it. */
    APP_PHOTO_DATA_MAX = 8192,                      /**< 8 MID B&W GIF frames = 1 HI color photo. */
    APP_PHOTO_MAX_OUT = 512,                        /**< Largest encoded side in pixels. */
    APP_PHOTO_MAGIC = 0x50,                         /**< 'P' */
    APP_PHOTO_VERSION = 3,                          /**< 2: lens byte; 3: color flag in it. */
    APP_PHOTO_RES_MASK = 0x7F,                      /**< header.res: lens ... */
    APP_PHOTO_COLOR_FLAG = 0x80,                    /**< ... and color film (v3). */
};

/** Looks. PLAIN is free; the others are store items. Any look works at any lens. */
typedef enum {
    APP_FILTER_NONE = 0,       /**< PLAIN: four shades of the pet's color (or color film). */
    APP_FILTER_DITHER,         /**< Ordered 4x4 dithering between the shades. */
    APP_FILTER_SCANLINES,      /**< Every other row one shade darker. */
    APP_FILTER_GREEN,          /**< Four pocket-console greens. */
    APP_FILTER_THERMAL,        /**< Heat-camera palette, contrast pushed. */
    APP_FILTER_GLITCH,         /**< VHS tears: row bands shifted, a color split. (Was EYE STICKERS.) */
    APP_FILTER_ASCII,          /**< 4x4 character cells picked by brightness. */
    APP_FILTER_HALFTONE,       /**< Dots on a staggered grid, sized by brightness. */
    APP_FILTER_EDGES,          /**< Glowing outlines on black. */
    APP_FILTER_POP,            /**< Bold poster tones / colors. */
    APP_FILTER_MIRROR,         /**< Left half reflected onto the right. */
    APP_FILTER_CRT,            /**< Old-TV stripes. */
    APP_FILTER_COUNT,          /**< At most 16: ownership is two bytes. */
} app_photo_filter_t;

typedef struct {
    uint8_t r, g, b;
} app_photo_rgb_t;

/** Stored header (12 bytes, little-endian seq), followed by `frames` packed frames. */
typedef struct {
    uint8_t magic;             /**< APP_PHOTO_MAGIC. */
    uint8_t version;           /**< APP_PHOTO_VERSION. */
    uint8_t filter;            /**< app_photo_filter_t, selects the palette. */
    uint8_t frames;            /**< 1 = photo, APP_PHOTO_GIF_FRAMES = GIF. */
    app_photo_rgb_t base;      /**< Pet color at capture, for the pet-colored palettes. */
    uint8_t res;               /**< app_photo_res_t, | APP_PHOTO_COLOR_FLAG for color film. */
    uint32_t seq;              /**< Capture order; larger is newer. */
} app_photo_header_t;

typedef struct {
    app_photo_header_t header;
    uint8_t data[APP_PHOTO_DATA_MAX];   /**< Frames back to back, see app_photo_frame(). */
} app_photo_t;

static inline int app_photo_side(int res)
{
    return APP_PHOTO_MIN_SIDE << (res < 0 ? 0 : (res >= APP_PHOTO_RES_COUNT ? APP_PHOTO_RES_COUNT - 1 : res));
}

static inline int app_photo_lens(const app_photo_header_t *h)
{
    return h->res & APP_PHOTO_RES_MASK;
}

static inline bool app_photo_is_color(const app_photo_header_t *h)
{
    return (h->res & APP_PHOTO_COLOR_FLAG) != 0;
}

/** Packed bytes of one frame, row-major, MSB-first: 2 bpp (B&W) or 4 bpp (color). */
static inline size_t app_photo_frame_bytes(int res, bool color)
{
    const size_t side = (size_t)app_photo_side(res);
    return side * side / (color ? 2U : 4U);
}

static inline size_t app_photo_header_frame_bytes(const app_photo_header_t *h)
{
    return app_photo_frame_bytes(app_photo_lens(h), app_photo_is_color(h));
}

/** Bytes a stored record occupies (header + frames). */
static inline size_t app_photo_record_size(const app_photo_header_t *h)
{
    return sizeof(app_photo_header_t) + (size_t)h->frames * app_photo_header_frame_bytes(h);
}

static inline uint8_t *app_photo_frame(app_photo_t *photo, int frame)
{
    return photo->data + (size_t)frame * app_photo_header_frame_bytes(&photo->header);
}

static inline const uint8_t *app_photo_frame_const(const app_photo_t *photo, int frame)
{
    return photo->data + (size_t)frame * app_photo_header_frame_bytes(&photo->header);
}

/** Palette index at (x, y) of a packed frame `side` pixels wide: 0..3 for
 * B&W, 0..15 for color. */
static inline uint8_t app_photo_pixel_at(const uint8_t *packed, int side, bool color, int x, int y)
{
    const int i = y * side + x;
    if (color) {
        return (uint8_t)((packed[i >> 1] >> ((i & 1) ? 0 : 4)) & 15U);
    }
    return (uint8_t)((packed[i >> 2] >> (6 - 2 * (i & 3))) & 3U);
}

/** True for a known version (2 or 3), lens and frame count whose data fits. */
bool app_photo_header_valid(const app_photo_header_t *header);

/** Names for UI and the web page (upper case), "" when out of range. */
const char *app_photo_filter_name(int filter);
const char *app_photo_res_name(int res);

/** Box-average a square luminance image `from` pixels wide down to `to`
 * pixels wide (`from` a multiple of `to`). */
void app_photo_downscale(const uint8_t *luma, int from, uint8_t *out, int to);

/** Map an 8-bit square luminance image (`side` 1..128) to shades 0..3, one
 * byte per pixel. Contrast is stretched to the image's own 4th..96th percentile. */
void app_photo_filter(const uint8_t *luma, int side, app_photo_filter_t filter, uint8_t *shades);

/** Color film: map a square YUV image (three `side`x`side` planes, Y then U
 * then V, U/V centered on 128) to indices into app_photo_color_palette with
 * ordered dithering. GAME-GREEN and THERMAL have no color version (callers
 * use B&W for them). Shares the B&W looks' contrast stretch and cells. */
void app_photo_filter_color(const uint8_t *yuv, int side, app_photo_filter_t filter, uint8_t *indices);
/** Looks that have a color version. */
static inline bool app_photo_filter_has_color(int filter)
{
    return filter != APP_FILTER_GREEN && filter != APP_FILTER_THERMAL;
}

/** The fixed 16-color retro palette of color film (index 0 black, 3 white). */
extern const app_photo_rgb_t app_photo_color_palette[APP_PHOTO_COLORS];

/** B&W palette (darkest first) for a look; `base` is the pet color. */
void app_photo_palette(app_photo_filter_t filter, app_photo_rgb_t base, app_photo_rgb_t out[APP_PHOTO_SHADES]);
/** The palette a stored photo is drawn with: 4 or 16 entries; returns the count. */
int app_photo_header_palette(const app_photo_header_t *h, app_photo_rgb_t out[APP_PHOTO_COLORS]);

/** Pack/unpack `pixels` (a multiple of 4) between one-byte indices and
 * 2 bpp (B&W) or 4 bpp (color). */
void app_photo_pack(const uint8_t *shades, int pixels, uint8_t *packed);
void app_photo_unpack(const uint8_t *packed, int pixels, uint8_t *shades);
void app_photo_pack4(const uint8_t *indices, int pixels, uint8_t *packed);

/** Encoders. `scale` (>= 1) multiplies the photo's side; the result must stay
 * within APP_PHOTO_MAX_OUT. Bounds size `out`; real GIFs are far smaller.
 * Return the byte count, or 0 when `cap` is too small or arguments are bad. */
size_t app_photo_png_bound(int res, int scale);   /* Covers B&W and color. */
size_t app_photo_gif_bound(int res, int frames, int scale);
/** Frame 0 as an indexed 2-bit PNG. */
size_t app_photo_encode_png(const app_photo_t *photo, int scale, uint8_t *out, size_t cap);
/** All frames as a looping GIF (`frame_ms` each); `work` holds
 * APP_PHOTO_GIF_WORK_BYTES for the LZW table. */
enum { APP_PHOTO_GIF_WORK_BYTES = 5003 * 6 };
size_t app_photo_encode_gif(const app_photo_t *photo, int scale, int frame_ms, void *work,
                            uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
