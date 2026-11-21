// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <string.h>
#include "private/pixel_draw.h"

#define PX_SCREEN 480
enum { GLYPH_W = 5, GLYPH_H = 7, GLYPH_ADVANCE = 6 };

static const Color s_palette[APP_COLOR_COUNT] = {
    {217, 255, 0, 255},    /* lime */
    {255, 76, 1, 255},     /* mosaico orange */
    {0, 229, 255, 255},    /* cyan */
    {255, 95, 210, 255},   /* pink */
    {240, 240, 232, 255},  /* white */
    {255, 176, 0, 255},    /* amber */
};

px_grid_t px_grid(app_pixel_style_t style)
{
    switch (style) {
    case APP_PIXEL_FINE: return (px_grid_t) {6, 1};
    case APP_PIXEL_SMOOTH: return (px_grid_t) {2, 0};
    default: return (px_grid_t) {12, 2};
    }
}

Color px_color(int index)
{
    index %= APP_COLOR_COUNT;
    return s_palette[index < 0 ? index + APP_COLOR_COUNT : index];
}

Color px_accent(int index)
{
    /* Orange (index 1) pairs with lime; every other color pairs with orange. */
    return ((index % APP_COLOR_COUNT) + APP_COLOR_COUNT) % APP_COLOR_COUNT == 1 ? s_palette[0] : s_palette[1];
}

Color px_scale(Color c, float k)
{
    float r = c.r * k, g = c.g * k, b = c.b * k;
    return (Color) {
        (unsigned char)(r > 255 ? 255 : r), (unsigned char)(g > 255 ? 255 : g),
        (unsigned char)(b > 255 ? 255 : b), 255
    };
}

void px_fill_shape(px_grid_t grid, float x0, float y0, float x1, float y1,
                   px_inside_fn inside, const void *shape, Color color)
{
    const int c = grid.cell > 0 ? grid.cell : 1;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > PX_SCREEN) {
        x1 = PX_SCREEN;
    }
    if (y1 > PX_SCREEN) {
        y1 = PX_SCREEN;
    }
    const int gx0 = (int)floorf(x0 / c), gx1 = (int)ceilf(x1 / c);
    const int gy0 = (int)floorf(y0 / c), gy1 = (int)ceilf(y1 / c);
    const float half = c * 0.5f;
    for (int gy = gy0; gy < gy1; ++gy) {
        const float py = gy * c + half;
        int run = -1;
        for (int gx = gx0; gx <= gx1; ++gx) {
            const bool in = gx < gx1 && inside(shape, gx * c + half, py);
            if (grid.gap) {
                if (in) {
                    DrawRectangle(gx * c, gy * c, c - grid.gap, c - grid.gap, color);
                }
                continue;
            }
            if (in && run < 0) {
                run = gx;
            } else if (!in && run >= 0) {
                DrawRectangle(run * c, gy * c, (gx - run) * c, c, color);
                run = -1;
            }
        }
    }
}

static const uint8_t *glyph(char ch)
{
    static const uint8_t digits[10][GLYPH_H] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31},
        {31, 2, 4, 2, 1, 17, 14}, {2, 6, 10, 18, 31, 2, 2}, {31, 16, 30, 1, 1, 17, 14},
        {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8}, {14, 17, 17, 14, 17, 17, 14},
        {14, 17, 17, 15, 1, 2, 12},
    };
    static const uint8_t letters[26][GLYPH_H] = {
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
        {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
        {7, 2, 2, 2, 2, 18, 12}, {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17}, {17, 17, 25, 21, 19, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
        {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4}, {17, 17, 17, 17, 17, 17, 14},
        {17, 17, 17, 17, 17, 10, 4}, {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
    };
    static const struct {
        char ch;
        uint8_t rows[GLYPH_H];
    } symbols[] = {
        {'+', {0, 4, 4, 31, 4, 4, 0}}, {'-', {0, 0, 0, 31, 0, 0, 0}},
        {'!', {4, 4, 4, 4, 4, 0, 4}}, {'?', {14, 17, 1, 2, 4, 0, 4}},
        {'/', {1, 1, 2, 4, 8, 16, 16}}, {':', {0, 12, 12, 0, 12, 12, 0}},
        {'.', {0, 0, 0, 0, 0, 12, 12}}, {'%', {24, 25, 2, 4, 8, 19, 3}},
        {'x', {0, 0, 17, 10, 4, 10, 17}},
    };
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'];
    }
    if (ch >= 'A' && ch <= 'Z') {
        return letters[ch - 'A'];
    }
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        if (symbols[i].ch == ch) {
            return symbols[i].rows;
        }
    }
    if (ch >= 'a' && ch <= 'z') {
        return letters[ch - 'a'];
    }
    return NULL;
}

void px_text(const char *text, int x, int y, int scale, Color color, bool gap)
{
    if (!text || scale < 1) {
        return;
    }
    const int g = gap && scale >= 4 ? 1 : 0;   /* Gaps make small text look dotted. */
    for (; *text; ++text, x += GLYPH_ADVANCE * scale) {
        const uint8_t *rows = glyph(*text);
        if (!rows) {
            continue;
        }
        for (int ry = 0; ry < GLYPH_H; ++ry) {
            for (int rx = 0; rx < GLYPH_W; ++rx) {
                if (rows[ry] & (1U << (GLYPH_W - 1 - rx))) {
                    DrawRectangle(x + rx * scale, y + ry * scale, scale - g, scale - g, color);
                }
            }
        }
    }
}

int px_text_width(const char *text, int scale)
{
    const int n = text ? (int)strlen(text) : 0;
    return n ? n * GLYPH_ADVANCE * scale - scale : 0;
}

void px_text_center(const char *text, int cx, int y, int scale, Color color, bool gap)
{
    px_text(text, cx - px_text_width(text, scale) / 2, y, scale, color, gap);
}

void px_icon(const char *const *rows, int x, int y, int scale, Color primary, Color secondary, bool gap)
{
    const int g = gap && scale >= 3 ? 1 : 0;
    for (int ry = 0; rows && rows[ry]; ++ry) {
        for (int rx = 0; rows[ry][rx]; ++rx) {
            const char p = rows[ry][rx];
            if (p == '#' || p == 'o') {
                DrawRectangle(x + rx * scale, y + ry * scale, scale - g, scale - g,
                              p == '#' ? primary : secondary);
            }
        }
    }
}

int px_icon_width(const char *const *rows)
{
    return rows && rows[0] ? (int)strlen(rows[0]) : 0;
}

int px_icon_height(const char *const *rows)
{
    int n = 0;
    while (rows && rows[n]) {
        ++n;
    }
    return n;
}

void px_box(int x, int y, int w, int h, int t, Color color)
{
    const int k = t > 0 ? t : 4;   /* Corner notch size. */
    if (t <= 0) {
        DrawRectangle(x + k, y, w - 2 * k, h, color);
        DrawRectangle(x, y + k, k, h - 2 * k, color);
        DrawRectangle(x + w - k, y + k, k, h - 2 * k, color);
        return;
    }
    DrawRectangle(x + k, y, w - 2 * k, t, color);
    DrawRectangle(x + k, y + h - t, w - 2 * k, t, color);
    DrawRectangle(x, y + k, t, h - 2 * k, color);
    DrawRectangle(x + w - t, y + k, t, h - 2 * k, color);
}

const char *const PX_ICON_BOLT[] = {
    "....##",
    "...##.",
    "..##..",
    ".#####",
    "...##.",
    "..##..",
    ".##...",
    NULL,
};

const char *const PX_ICON_SHOE[] = {
    "..###...",
    "..###...",
    "..####..",
    "..#####.",
    "########",
    "oooooooo",
    NULL,
};

const char *const PX_ICON_HEART[] = {
    ".##.##.",
    "#######",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
    NULL,
};

const char *const PX_ICON_HEART_EMPTY[] = {
    ".##.##.",
    "#..#..#",
    "#.....#",
    ".#...#.",
    "..#.#..",
    "...#...",
    NULL,
};

const char *const PX_ICON_BATTERY_LOW[] = {
    "..###..",
    "#######",
    "#.....#",
    "#.....#",
    "#.....#",
    "#ooooo#",
    "#######",
    NULL,
};

const char *const PX_ICON_FOOD[APP_FOOD_COUNT][13] = {
    {
        "...##...",
        "########",
        "#......#",
        "#...oo.#",
        "#..oo..#",
        "#.oooo.#",
        "#..oo..#",
        "#.oo...#",
        "#......#",
        "########",
        NULL,
    },
    {
        ".#.#.#.#.",
        "#########",
        "##ooooo##",
        "#.o###o.#",
        "##o#.#o##",
        "#.o###o.#",
        "##ooooo##",
        "#########",
        ".#.#.#.#.",
        NULL,
    },
    {
        "...####...",
        "##########",
        "#........#",
        "#.....oo.#",
        "#....oo..#",
        "#...oo...#",
        "#..oooooo#",
        "#.....oo.#",
        "#....oo..#",
        "#...oo...#",
        "#........#",
        "##########",
        NULL,
    },
};
