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
    "PLAIN", "DITHER", "SCANLINES", "GAME-GREEN", "THERMAL", "GLITCH", "ASCII", "HALFTONE",
    "EDGES", "POP", "MIRROR", "CRT",
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


/* Color film's palette: index 0 black and 3 white, so the
 * cell looks can draw with the same indices as B&W shades 0 and 3. */
const app_photo_rgb_t app_photo_color_palette[APP_PHOTO_COLORS] = {
    {0, 0, 0}, {84, 84, 92}, {168, 168, 176}, {255, 255, 255},
    {132, 24, 40}, {228, 56, 48}, {244, 144, 48}, {252, 228, 84},
    {32, 104, 52}, {104, 196, 72}, {28, 40, 112}, {60, 112, 228},
    {88, 208, 232}, {128, 52, 160}, {244, 132, 180}, {200, 140, 100},
};

bool app_photo_header_valid(const app_photo_header_t *h)
{
    if (!h || h->magic != APP_PHOTO_MAGIC || h->filter >= APP_FILTER_COUNT
            || app_photo_lens(h) >= APP_PHOTO_RES_COUNT) {
        return false;
    }
    /* v2 photos have no color flag; v3 may. */
    if (h->version != APP_PHOTO_VERSION && !(h->version == 2 && !app_photo_is_color(h))) {
        return false;
    }
    if (app_photo_is_color(h) && !app_photo_filter_has_color(h->filter)) {
        return false;
    }
    if (h->frames == APP_PHOTO_GIF_FRAMES) {
        return app_photo_lens(h) <= (app_photo_is_color(h) ? APP_PHOTO_GIF_MAX_RES_COLOR : APP_PHOTO_GIF_MAX_RES);
    }
    return h->frames == 1;
}

int app_photo_header_palette(const app_photo_header_t *h, app_photo_rgb_t out[APP_PHOTO_COLORS])
{
    if (app_photo_is_color(h)) {
        memcpy(out, app_photo_color_palette, sizeof(app_photo_color_palette));
        return APP_PHOTO_COLORS;
    }
    app_photo_palette((app_photo_filter_t)h->filter, h->base, out);
    return APP_PHOTO_SHADES;
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

static void glitch_rows(uint8_t *px, int side, bool invert_band);
static int edge_at(const uint8_t *luma, int side, int x, int y, int lo, int span);
static void mirror_left(uint8_t *px, int side);

/* ---- Color film ---------------------------------------------------- */

typedef struct {
    int r, g, b;
} rgb_t;

/* BT.601 YUV -> RGB; chroma is boosted for a punchier, retro look. */
static rgb_t yuv_to_rgb(int y, int u, int v)
{
    u = (u - 128) * 3 / 2;
    v = (v - 128) * 3 / 2;
    return (rgb_t) {
        y + (1436 * v) / 1024, y - (352 * u + 731 * v) / 1024, y + (1815 * u) / 1024
    };
}

/* Half the B&W contrast stretch: a full stretch pushes light colors to white. */
static int color_luma(int y, int lo, int span)
{
    return (y + stretched(y, lo, span)) / 2;
}

static uint8_t nearest_color(rgb_t c)
{
    int best = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < APP_PHOTO_COLORS; ++i) {
        const app_photo_rgb_t *p = &app_photo_color_palette[i];
        const int dr = c.r - p->r, dg = c.g - p->g, db = c.b - p->b;
        const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;   /* Rough perceptual weights. */
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

/* Mean color of a clipped cell, luma stretched. */
static rgb_t cell_color(const uint8_t *yuv, int side, int x0, int y0, int c, int lo, int span)
{
    const int n = side * side;
    int sy = 0, su = 0, sv = 0, k = 0;
    for (int y = y0 < 0 ? 0 : y0; y < y0 + c && y < side; ++y) {
        for (int x = x0 < 0 ? 0 : x0; x < x0 + c && x < side; ++x) {
            const int i = y * side + x;
            sy += yuv[i];
            su += yuv[n + i];
            sv += yuv[2 * n + i];
            ++k;
        }
    }
    if (!k) {
        return (rgb_t) {0, 0, 0};
    }
    return yuv_to_rgb(color_luma(sy / k, lo, span), su / k, sv / k);
}

/* Lift a cell's color toward full brightness without changing its hue:
 * scale every channel by the same factor, at most 2x, so the brightest
 * channel stays within 255. (Scaling by 255/luma blew grays out to white
 * and soft colors into neon.) */
static rgb_t lift(rgb_t c)
{
    c.r = c.r < 0 ? 0 : c.r;
    c.g = c.g < 0 ? 0 : c.g;
    c.b = c.b < 0 ? 0 : c.b;
    const int hi = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    if (hi <= 0) {
        return c;
    }
    const int k = hi * 2 > 255 ? 255 * 256 / hi : 512;   /* Factor in 1/256ths. */
    return (rgb_t) {c.r * k / 256, c.g * k / 256, c.b * k / 256};
}

/* Cell looks in color: ASCII glyphs and halftone dots take their cell's
 * color (at full brightness) on black. */
static void color_cells(const uint8_t *yuv, int side, app_photo_filter_t filter, int lo, int span, uint8_t *out)
{
    memset(out, 0, (size_t)side * side);
    const bool dots = filter == APP_FILTER_HALFTONE;
    const int c = dots ? (side > REF_SIDE ? 8 : 4) : ASCII_CELL;
    for (int row = 0; row * c < side; ++row) {
        const int off = dots && (row & 1) ? c / 2 : 0;
        for (int col = dots ? -1 : 0; col * c + off < side; ++col) {
            const int x0 = col * c + off, y0 = row * c;
            rgb_t m = cell_color(yuv, side, x0, y0, c, lo, span);
            const int lum = (m.r * 3 + m.g * 6 + m.b) / 10;
            const int v = lum < 0 ? 0 : (lum > 255 ? 255 : lum);
            m = lift(m);   /* Brighter ink (size carries the luma), same hue. */
            const uint8_t ink = v ? nearest_color(m) : 0;
            const float r = (float)c * 0.72f * sqrtf((float)v / 255.0f);
            const int level = v * ASCII_LEVELS / 256;
            const float cx = (float)x0 + (float)c / 2.0f, cy = (float)y0 + (float)c / 2.0f;
            for (int y = y0; y < y0 + c && y < side; ++y) {
                for (int x = x0 < 0 ? 0 : x0; x < x0 + c && x < side; ++x) {
                    bool on;
                    if (dots) {
                        const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
                        on = sqrtf(dx * dx + dy * dy) < r;
                    } else {
                        on = (s_glyphs[level][y - y0] >> (3 - (x - x0))) & 1U;
                    }
                    out[y * side + x] = on ? ink : 0;
                }
            }
        }
    }
}

void app_photo_filter_color(const uint8_t *yuv, int side, app_photo_filter_t filter, uint8_t *out)
{
    if (!yuv || !out || side <= 0 || side > APP_PHOTO_MAX_SIDE) {
        return;
    }
    const int n = side * side;
    int lo, span;
    stretch_range(yuv, n, HIST_LO_PCT, HIST_HI_PCT, &lo, &span);
    if (filter == APP_FILTER_ASCII || filter == APP_FILTER_HALFTONE) {
        color_cells(yuv, side, filter, lo, span, out);
        return;
    }
    /* Ordered dithering between palette colors; DITHER spreads it wider. */
    const int amp = filter == APP_FILTER_DITHER ? 96 : 40;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int i = y * side + x;
            rgb_t c = yuv_to_rgb(color_luma(yuv[i], lo, span), yuv[n + i], yuv[2 * n + i]);
            const int t = (s_bayer[y & 3][x & 3] * 2 - 15) * amp / 32;
            c.r += t;
            c.g += t;
            c.b += t;
            if (filter == APP_FILTER_GLITCH) {
                const int j = y * side + (x + 2 < side ? x + 2 : x);
                c.r = yuv_to_rgb(color_luma(yuv[j], lo, span), yuv[n + j], yuv[2 * n + j]).r;
            }
            if (filter == APP_FILTER_SCANLINES && (y & 1)) {
                c = (rgb_t) {c.r * 5 / 10, c.g * 5 / 10, c.b * 5 / 10};
            } else if (filter == APP_FILTER_EDGES) {
                /* Outlines in the scene's own colors, on black. */
                out[i] = edge_at(yuv, side, x, y, lo, span) > 80 ? nearest_color(lift(c)) : 0;
                continue;
            } else if (filter == APP_FILTER_POP) {
                c = (rgb_t) {c.r > 140 ? 255 : (c.r > 70 ? 128 : 0), c.g > 140 ? 255 : (c.g > 70 ? 128 : 0),
                             c.b > 140 ? 255 : (c.b > 70 ? 128 : 0)};
            } else if (filter == APP_FILTER_CRT) {
                /* Each column glows in one phosphor color; odd rows dimmer. */
                const int k = (y & 1) ? 6 : 10;
                c = (rgb_t) {x % 3 == 0 ? c.r * 12 / 10 : c.r * 7 / 10, x % 3 == 1 ? c.g * 12 / 10 : c.g * 7 / 10,
                             x % 3 == 2 ? c.b * 12 / 10 : c.b * 7 / 10};
                c = (rgb_t) {c.r * k / 10, c.g * k / 10, c.b * k / 10};
            }
            out[i] = nearest_color(c);
        }
    }
    if (filter == APP_FILTER_GLITCH) {
        glitch_rows(out, side, false);   /* Color indices are not shades: no invert. */
    }
    if (filter == APP_FILTER_MIRROR) {
        mirror_left(out, side);
    }
}

/* ---- B&W cell looks ------------------------------------------------- */

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

/* GLITCH: tear bands of rows sideways (wrapping), like a worn VHS tape. The
 * seed comes from the picture, so every GIF frame tears differently. One band
 * is inverted (B&W) for a flash of noise. */
static void glitch_rows(uint8_t *px, int side, bool invert_band)
{
    uint32_t seed = 2166136261U;
    for (int i = 0; i < side * side; i += 97) {
        seed = (seed ^ px[i]) * 16777619U;
    }
    const int band = side >= 32 ? side / 16 : 2;
    uint8_t row[APP_PHOTO_MAX_SIDE];
    const uint32_t flash = seed % (uint32_t)(side / band);
    for (int b0 = 0; b0 * band < side; ++b0) {
        const uint32_t h = (seed ^ (uint32_t)b0 * 2654435761U) * 2246822519U;
        const int shift = (h % 2U == 0U) ? (int)((h >> 8) % (uint32_t)(side / 3 + 1)) - side / 6 : 0;
        for (int y = b0 * band; y < (b0 + 1) * band && y < side; ++y) {
            uint8_t *r = px + y * side;
            if (shift) {
                for (int x = 0; x < side; ++x) {
                    row[x] = r[((x - shift) % side + side) % side];
                }
                memcpy(r, row, (size_t)side);
            }
            if (invert_band && (uint32_t)b0 == flash) {
                for (int x = 0; x < side; ++x) {
                    r[x] = (uint8_t)(3 - (r[x] & 3U));
                }
            }
        }
    }
}

/* Edge strength at (x, y): sum of the horizontal and vertical brightness
 * steps across the pixel (stretched luma, clamped at the borders). */
static int edge_at(const uint8_t *luma, int side, int x, int y, int lo, int span)
{
    const int xl = x > 0 ? x - 1 : x, xr = x < side - 1 ? x + 1 : x;
    const int yu = y > 0 ? y - 1 : y, yd = y < side - 1 ? y + 1 : y;
    const int gx = stretched(luma[y * side + xr], lo, span) - stretched(luma[y * side + xl], lo, span);
    const int gy = stretched(luma[yd * side + x], lo, span) - stretched(luma[yu * side + x], lo, span);
    return (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
}

/* MIRROR: copy the left half, reflected, over the right half. */
static void mirror_left(uint8_t *px, int side)
{
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side / 2; ++x) {
            px[y * side + side - 1 - x] = px[y * side + x];
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
            if (filter == APP_FILTER_EDGES) {
                const int e = edge_at(luma, side, x, y, lo, span);
                shade = e > 150 ? 3 : (e > 90 ? 2 : (e > 50 ? 1 : 0));
            } else if (filter == APP_FILTER_POP) {
                shade = v < 70 ? 0 : (v < 150 ? 2 : 3);   /* Hard poster steps, no mid-dark. */
            } else if (filter == APP_FILTER_DITHER) {
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
                if (filter == APP_FILTER_CRT && (x % 3 == 2 || (y & 1)) && shade > 0) {
                    --shade;   /* Dark gaps between "phosphor" columns and rows. */
                }
            }
            shades[y * side + x] = (uint8_t)(shade > 3 ? 3 : shade);
        }
    }
    if (filter == APP_FILTER_GLITCH) {
        glitch_rows(shades, side, true);
    }
    if (filter == APP_FILTER_MIRROR) {
        mirror_left(shades, side);
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

void app_photo_pack4(const uint8_t *indices, int pixels, uint8_t *packed)
{
    for (int i = 0; i < pixels / 2; ++i) {
        packed[i] = (uint8_t)(((indices[i * 2] & 15U) << 4) | (indices[i * 2 + 1] & 15U));
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
