// SPDX-License-Identifier: Apache-2.0
/* Looks and palettes: camera luminance -> four shades, at any lens. */
#include <math.h>
#include <string.h>
#include "include/app_photo.h"

enum {
    HIST_LO_PCT = 4,          /* Contrast stretch percentiles. */
    HIST_HI_PCT = 96,
    THERMAL_LO_PCT = 10,      /* Thermal pushes contrast harder. */
    THERMAL_HI_PCT = 90,
    MIN_SPAN = 24,            /* Flat scenes are not stretched into noise. */
    ASCII_CELL = 4,           /* Character cell in photo pixels. */
    ASCII_LEVELS = 8,
    REF_SIDE = 64,            /* Sticker coordinates are given at 64x64. */
};

static const char *const s_names[APP_FILTER_COUNT] = {
    "PLAIN", "DITHER", "SCANLINES", "GAME-GREEN", "THERMAL", "EYE STICKERS", "ASCII", "HALFTONE",
};
static const char *const s_res_names[APP_PHOTO_RES_COUNT] = {"LO RES", "MID RES", "HI RES"};

/* 4x4 Bayer thresholds, 0..15. */
static const uint8_t s_bayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

/* Character ramp " .:-+x0@" as 4x4 cells (3x3 glyph + spacing), rows MSB-left. */
static const uint8_t s_glyphs[ASCII_LEVELS][4] = {
    {0x0, 0x0, 0x0, 0x0}, {0x0, 0x0, 0x4, 0x0}, {0x4, 0x0, 0x4, 0x0}, {0x0, 0xE, 0x0, 0x0},
    {0x4, 0xE, 0x4, 0x0}, {0xA, 0x4, 0xA, 0x0}, {0xE, 0xA, 0xE, 0x0}, {0xE, 0xE, 0xE, 0x0},
};

/* Sticker: one of the pet's eyes, '#' = bright fill, 'o' = dark outline. */
static const char *const s_sticker_eye[] = {
    " ooooooo ",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    "o#######o",
    " ooooooo ",
};
enum { STICKER_W = 9, STICKER_H = 11, STICKER_X = 40, STICKER_GAP = 3, STICKER_Y = 4 };

bool app_photo_header_valid(const app_photo_header_t *h)
{
    if (!h || h->magic != APP_PHOTO_MAGIC || h->version != APP_PHOTO_VERSION || h->filter >= APP_FILTER_COUNT
            || h->res >= APP_PHOTO_RES_COUNT) {
        return false;
    }
    if (h->frames == APP_PHOTO_GIF_FRAMES) {
        return h->res <= APP_PHOTO_GIF_MAX_RES;
    }
    return h->frames == 1;
}

const char *app_photo_filter_name(int filter)
{
    return filter >= 0 && filter < APP_FILTER_COUNT ? s_names[filter] : "";
}

const char *app_photo_res_name(int res)
{
    return res >= 0 && res < APP_PHOTO_RES_COUNT ? s_res_names[res] : "";
}

void app_photo_downscale(const uint8_t *luma, int from, uint8_t *out, int to)
{
    if (!luma || !out || to <= 0 || from < to || from % to) {
        return;
    }
    const int k = from / to;
    for (int y = 0; y < to; ++y) {
        for (int x = 0; x < to; ++x) {
            uint32_t sum = 0;
            for (int j = 0; j < k; ++j) {
                const uint8_t *row = luma + (size_t)(y * k + j) * from + (size_t)x * k;
                for (int i = 0; i < k; ++i) {
                    sum += row[i];
                }
            }
            out[y * to + x] = (uint8_t)(sum / (uint32_t)(k * k));
        }
    }
}

/* Luminance value below which `pct` percent of the pixels fall. */
static int percentile(const uint16_t hist[256], int total, int pct)
{
    const int target = total * pct / 100;
    int seen = 0;
    for (int v = 0; v < 256; ++v) {
        seen += hist[v];
        if (seen > target) {
            return v;
        }
    }
    return 255;
}

/* The 512-byte histogram is a leaf-frame local: callers run on the game loop
 * (20 KB stack) or an HTTP handler, never in an ISR. */
static void stretch_range(const uint8_t *luma, int n, int lo_pct, int hi_pct, int *lo, int *span)
{
    uint16_t hist[256];
    memset(hist, 0, sizeof(hist));
    for (int i = 0; i < n; ++i) {
        ++hist[luma[i]];
    }
    int a = percentile(hist, n, lo_pct), b = percentile(hist, n, hi_pct);
    if (b - a < MIN_SPAN) {
        const int mid = (a + b) / 2;
        a = mid - MIN_SPAN / 2;
        b = mid + MIN_SPAN / 2;
    }
    *lo = a;
    *span = b - a;
}

static int stretched(int l, int lo, int span)
{
    const int v = (l - lo) * 255 / span;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* Mean stretched brightness of the clipped square [x0,x0+c) x [y0,y0+c). */
static int cell_mean(const uint8_t *luma, int side, int x0, int y0, int c, int lo, int span)
{
    int sum = 0, n = 0;
    for (int y = y0 < 0 ? 0 : y0; y < y0 + c && y < side; ++y) {
        for (int x = x0 < 0 ? 0 : x0; x < x0 + c && x < side; ++x) {
            sum += luma[y * side + x];
            ++n;
        }
    }
    return n ? stretched(sum / n, lo, span) : 0;
}

static void ascii(const uint8_t *luma, int side, int lo, int span, uint8_t *shades)
{
    for (int cy = 0; cy < side; cy += ASCII_CELL) {
        for (int cx = 0; cx < side; cx += ASCII_CELL) {
            const int level = cell_mean(luma, side, cx, cy, ASCII_CELL, lo, span) * ASCII_LEVELS / 256;
            const int ink = level ? 1 + (level - 1) * 2 / (ASCII_LEVELS - 2) : 0;
            for (int y = 0; y < ASCII_CELL && cy + y < side; ++y) {
                for (int x = 0; x < ASCII_CELL && cx + x < side; ++x) {
                    const bool on = (s_glyphs[level][y] >> (3 - x)) & 1U;
                    shades[(cy + y) * side + cx + x] = (uint8_t)(on ? (ink > 3 ? 3 : ink) : 0);
                }
            }
        }
    }
}

/* Bright dots on a staggered grid; area follows brightness, edges get a dim
 * shade so small dots still read as round. */
static void halftone(const uint8_t *luma, int side, int lo, int span, uint8_t *shades)
{
    const int c = side > REF_SIDE ? 8 : 4;
    memset(shades, 0, (size_t)side * side);
    for (int row = 0; row * c < side; ++row) {
        const int off = (row & 1) ? c / 2 : 0;
        for (int col = -1; col * c + off < side; ++col) {
            const int x0 = col * c + off, y0 = row * c;
            const float v = (float)cell_mean(luma, side, x0, y0, c, lo, span) / 255.0f;
            const float r = (float)c * 0.72f * sqrtf(v);
            const float cx = (float)x0 + (float)c / 2.0f, cy = (float)y0 + (float)c / 2.0f;
            for (int y = y0; y < y0 + c && y < side; ++y) {
                for (int x = x0 < 0 ? 0 : x0; x < x0 + c && x < side; ++x) {
                    const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
                    const float d = sqrtf(dx * dx + dy * dy);
                    shades[y * side + x] = d < r - 0.5f ? 3 : (d < r + 0.5f ? 1 : 0);
                }
            }
        }
    }
}

/* Stickers are laid out at 64x64 and resampled to the lens. */
static void draw_stickers(uint8_t *shades, int side)
{
    for (int eye = 0; eye < 2; ++eye) {
        const int ex = STICKER_X + eye * (STICKER_W + STICKER_GAP);
        for (int py = STICKER_Y * side / REF_SIDE; py < (STICKER_Y + STICKER_H) * side / REF_SIDE; ++py) {
            for (int px = ex * side / REF_SIDE; px < (ex + STICKER_W) * side / REF_SIDE; ++px) {
                const int bx = px * REF_SIDE / side - ex, by = py * REF_SIDE / side - STICKER_Y;
                if (bx < 0 || by < 0 || bx >= STICKER_W || by >= STICKER_H || px >= side || py >= side) {
                    continue;
                }
                const char c = s_sticker_eye[by][bx];
                if (c != ' ') {
                    shades[py * side + px] = c == '#' ? 3 : 0;
                }
            }
        }
    }
}

void app_photo_filter(const uint8_t *luma, int side, app_photo_filter_t filter, uint8_t *shades)
{
    if (!luma || !shades || side <= 0 || side > APP_PHOTO_MAX_SIDE) {
        return;
    }
    const int n = side * side;
    int lo, span;
    if (filter == APP_FILTER_THERMAL) {
        stretch_range(luma, n, THERMAL_LO_PCT, THERMAL_HI_PCT, &lo, &span);
    } else {
        stretch_range(luma, n, HIST_LO_PCT, HIST_HI_PCT, &lo, &span);
    }
    if (filter == APP_FILTER_ASCII) {
        ascii(luma, side, lo, span, shades);
        return;
    }
    if (filter == APP_FILTER_HALFTONE) {
        halftone(luma, side, lo, span, shades);
        return;
    }
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int v = stretched(luma[y * side + x], lo, span);
            int shade;
            if (filter == APP_FILTER_DITHER) {
                const int scaled = v * (APP_PHOTO_SHADES - 1);
                shade = scaled / 255;
                if ((scaled % 255) * 16 / 255 > s_bayer[y & 3][x & 3]) {
                    ++shade;
                }
            } else {
                shade = (v * APP_PHOTO_SHADES) >> 8;
                if (filter == APP_FILTER_SCANLINES && (y & 1) && shade > 0) {
                    --shade;
                }
            }
            shades[y * side + x] = (uint8_t)(shade > 3 ? 3 : shade);
        }
    }
    if (filter == APP_FILTER_STICKERS && side >= APP_PHOTO_MIN_SIDE) {
        draw_stickers(shades, side);
    }
}

static app_photo_rgb_t scale_rgb(app_photo_rgb_t c, int pct)
{
    return (app_photo_rgb_t) {
        (uint8_t)(c.r * pct / 100), (uint8_t)(c.g * pct / 100), (uint8_t)(c.b * pct / 100)
    };
}

void app_photo_palette(app_photo_filter_t filter, app_photo_rgb_t base, app_photo_rgb_t out[APP_PHOTO_SHADES])
{
    static const app_photo_rgb_t green[APP_PHOTO_SHADES] = {
        {20, 44, 20}, {52, 98, 44}, {124, 164, 48}, {184, 214, 104},
    };
    static const app_photo_rgb_t thermal[APP_PHOTO_SHADES] = {
        {24, 8, 64}, {164, 24, 120}, {248, 112, 24}, {255, 236, 96},
    };
    static const int levels[APP_PHOTO_SHADES] = {7, 35, 67, 100};
    for (int i = 0; i < APP_PHOTO_SHADES; ++i) {
        switch (filter) {
        case APP_FILTER_GREEN: out[i] = green[i]; break;
        case APP_FILTER_THERMAL: out[i] = thermal[i]; break;
        default: out[i] = scale_rgb(base, levels[i]); break;
        }
    }
}

void app_photo_pack(const uint8_t *shades, int pixels, uint8_t *packed)
{
    for (int i = 0; i < pixels / 4; ++i) {
        const uint8_t *s = &shades[i * 4];
        packed[i] = (uint8_t)(((s[0] & 3U) << 6) | ((s[1] & 3U) << 4) | ((s[2] & 3U) << 2) | (s[3] & 3U));
    }
}

void app_photo_unpack(const uint8_t *packed, int pixels, uint8_t *shades)
{
    for (int i = 0; i < pixels / 4; ++i) {
        const uint8_t b = packed[i];
        shades[i * 4] = (uint8_t)(b >> 6);
        shades[i * 4 + 1] = (uint8_t)((b >> 4) & 3U);
        shades[i * 4 + 2] = (uint8_t)((b >> 2) & 3U);
        shades[i * 4 + 3] = (uint8_t)(b & 3U);
    }
}
