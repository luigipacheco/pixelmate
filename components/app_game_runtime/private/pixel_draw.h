// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Pixel-art drawing helpers on top of the Raylib compatibility layer.
 * Shapes are sampled on a screen-aligned grid whose cell size comes from the
 * pixel style, so CHUNKY/FINE/SMOOTH share one code path. Stateless; call only
 * from the render loop between BeginDrawing() and EndDrawing(). */
#include <stdbool.h>
#include "raylib_lite_raylib.h"
#include "../../app_game/include/app_game.h"

typedef struct {
    int cell;   /* Grid pitch in pixels. */
    int gap;    /* Dark LCD gap between cells in pixels (0 = continuous). */
} px_grid_t;

/** Return the grid for a pixel style. */
px_grid_t px_grid(app_pixel_style_t style);

Color px_color(int index);          /* Palette entry, index wraps. */
Color px_accent(int index);         /* Secondary color that contrasts the palette entry. */
Color px_scale(Color c, float k);   /* Multiply RGB by k (0..1+), saturating. */

/** Return true when screen point (x, y) lies inside the shape. */
typedef bool (*px_inside_fn)(const void *shape, float x, float y);

/** Fill every grid cell in [x0,x1)x[y0,y1) whose center is inside the shape. */
void px_fill_shape(px_grid_t grid, float x0, float y0, float x1, float y1,
                   px_inside_fn inside, const void *shape, Color color);

/** 5x7 font, upper-case letters, digits and " +-!?/:.%x". Scale is pixels per dot. */
void px_text(const char *text, int x, int y, int scale, Color color, bool gap);
int px_text_width(const char *text, int scale);
void px_text_center(const char *text, int cx, int y, int scale, Color color, bool gap);

/** Draw a bitmap: '#' = primary, 'o' = secondary, anything else transparent. */
void px_icon(const char *const *rows, int x, int y, int scale, Color primary, Color secondary, bool gap);
int px_icon_width(const char *const *rows);
int px_icon_height(const char *const *rows);

/** Pixel-art box with clipped corners; thickness <= 0 fills it. */
void px_box(int x, int y, int w, int h, int thickness, Color color);

/* NULL-terminated icon bitmaps. */
extern const char *const PX_ICON_BOLT[];
extern const char *const PX_ICON_SHOE[];
extern const char *const PX_ICON_HEART[];
extern const char *const PX_ICON_HEART_EMPTY[];
extern const char *const PX_ICON_BATTERY_LOW[];
extern const char *const PX_ICON_LOCK[];
extern const char *const PX_ICON_EYE[];
extern const char *const PX_ICON_NOTE[];
extern const char *const PX_ICON_STAR[];
extern const char *const PX_ICON_FOOD[APP_FOOD_COUNT][13];
