// SPDX-License-Identifier: Apache-2.0
/* SPI LCD presenter for a virtual Game surface with centered letterboxing.
 * esp_display_present owns DMA buffers, callbacks and transfer fencing.
 * A successful present therefore means the panel transfer has completed. */
#include "cores3_video.h"
#include "cores3_viewport.h"
#include "cores3_strip.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_display_present.h"

#define CORES3_LCD_WIDTH 320U
#define CORES3_LCD_HEIGHT 240U
/* The CoreS3 profile's SPI bus max_transfer_sz is 3600 bytes: 5 rows of
 * 320 RGB565 pixels are 3200 bytes. */
#define CORES3_STRIP_ROWS 5U
#define CORES3_FRAME_TOKEN ((uintptr_t)0xB033U)

struct cores3_video {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    uint16_t logical_width;
    uint16_t logical_height;
    uint16_t view_x;
    uint16_t view_y;
    uint16_t view_width;
    uint16_t view_height;
    uint16_t *frame;
    esp_display_presenter_t *presenter;
    bool acquired;
    bool failed;
};

static raylib_lite_result_t result_from_esp(esp_err_t err)
{
    if (err == ESP_OK) return RAYLIB_LITE_OK;
    if (err == ESP_ERR_TIMEOUT) return RAYLIB_LITE_TIMEOUT;
    if (err == ESP_ERR_NO_MEM) return RAYLIB_LITE_NO_MEMORY;
    return RAYLIB_LITE_PLATFORM_ERROR;
}

static raylib_lite_result_t get_info(void *ctx, raylib_lite_video_info_t *out)
{
    cores3_video_t *video = ctx;
    if (!video || !out) return RAYLIB_LITE_INVALID_ARGUMENT;
    *out = (raylib_lite_video_info_t) {
        .width = video->logical_width,
        .height = video->logical_height,
        .stride_pixels = video->logical_width,
        .format = RAYLIB_LITE_PIXEL_RGB565_NATIVE,
    };
    return RAYLIB_LITE_OK;
}

static raylib_lite_result_t acquire(void *ctx, raylib_lite_frame_t *out)
{
    cores3_video_t *video = ctx;
    if (!video || !out) return RAYLIB_LITE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (video->failed) return RAYLIB_LITE_PLATFORM_ERROR;
    if (video->acquired) return RAYLIB_LITE_INVALID_STATE;
    video->acquired = true;
    *out = (raylib_lite_frame_t) {
        .pixels = video->frame,
        .width = video->logical_width,
        .height = video->logical_height,
        .stride_pixels = video->logical_width,
        .token = CORES3_FRAME_TOKEN,
    };
    return RAYLIB_LITE_OK;
}

static void fill_strip(cores3_video_t *video, uint16_t *pixels, uint16_t top, uint16_t count)
{
    const cores3_viewport_t view = {
        .logical_width = video->logical_width,
        .logical_height = video->logical_height,
        .lcd_width = CORES3_LCD_WIDTH,
        .lcd_height = CORES3_LCD_HEIGHT,
        .view_x = video->view_x,
        .view_y = video->view_y,
        .view_width = video->view_width,
        .view_height = video->view_height,
    };
    cores3_fill_strip_pixels(&view, video->frame, pixels,
                           top, count, false);
}

static raylib_lite_result_t present(void *ctx, raylib_lite_frame_t *frame)
{
    cores3_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    bool valid = frame && video->acquired &&
        frame->pixels == video->frame && frame->token == CORES3_FRAME_TOKEN &&
        frame->width == video->logical_width &&
        frame->height == video->logical_height &&
        frame->stride_pixels == video->logical_width;
    video->acquired = false;
    if (frame) memset(frame, 0, sizeof(*frame));
    if (!valid) return RAYLIB_LITE_INVALID_STATE;
    if (video->failed) return RAYLIB_LITE_PLATFORM_ERROR;

    size_t areas = 0;
    bool full = false;
    esp_err_t err = esp_display_presenter_begin_next_frame(
        video->presenter, NULL, NULL, 0, &areas, &full);
    if (err != ESP_OK) goto failed;
    for (uint16_t y = 0; y < CORES3_LCD_HEIGHT;) {
        esp_display_presenter_buffer_t lease = {0};
        err = esp_display_presenter_acquire_buffer(video->presenter, &lease);
        if (err != ESP_OK) goto cancel;
        const size_t stride = CORES3_LCD_WIDTH * sizeof(uint16_t);
        size_t rows = 0;
        size_t remaining = CORES3_LCD_HEIGHT - y;
        if (!lease.lease_id || !lease.surface.pixels || !lease.resolve_rows ||
                lease.capacity_bytes < stride) {
            err = ESP_FAIL;
            goto cancel;
        }
        err = lease.resolve_rows(lease.resolve_rows_ctx, lease.lease_id,
                                  stride, remaining, &rows);
        if (err != ESP_OK) goto cancel;
        if (!rows || rows > remaining || rows > lease.capacity_bytes / stride) {
            err = ESP_FAIL;
            goto cancel;
        }
        fill_strip(video, lease.surface.pixels, y, (uint16_t)rows);
        const esp_display_present_area_t area = {
            .x1 = 0, .y1 = y, .x2 = CORES3_LCD_WIDTH - 1,
            .y2 = y + rows - 1,
        };
        err = esp_display_presenter_submit_buffer(video->presenter, &lease, &area, stride);
        if (err != ESP_OK) goto cancel;
        y += (uint16_t)rows;
    }
    const esp_display_presenter_submit_t done = {
        .coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL,
    };
    err = esp_display_presenter_commit_frame(video->presenter, &done);
    if (err != ESP_OK) goto cancel;
    err = esp_display_presenter_quiesce(video->presenter, 1500);
    if (err != ESP_OK) goto failed;
    return RAYLIB_LITE_OK;
cancel:
    esp_display_presenter_cancel_frame(video->presenter);
failed:
    video->failed = true;
    return result_from_esp(err);
}

static void discard(void *ctx, raylib_lite_frame_t *frame)
{
    cores3_video_t *video = ctx;
    if (video) video->acquired = false;
    if (frame) memset(frame, 0, sizeof(*frame));
}

static raylib_lite_result_t flush(void *ctx, uint32_t timeout_ms)
{
    cores3_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    if (video->acquired) return RAYLIB_LITE_INVALID_STATE;
    return result_from_esp(esp_display_presenter_quiesce(video->presenter, timeout_ms));
}

raylib_lite_result_t cores3_video_open(
    esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io,
    uint16_t width, uint16_t height, bool swap_bytes, cores3_video_t **out)
{
    if (!panel || !io || !width || !height || !out)
        return RAYLIB_LITE_INVALID_ARGUMENT;
    *out = NULL;
    /* Keep control state in internal RAM; the presenter owns ISR state. */
    cores3_video_t *video = heap_caps_calloc(
        1, sizeof(*video), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!video) return RAYLIB_LITE_NO_MEMORY;
    video->panel = panel;
    video->io = io;
    video->logical_width = width;
    video->logical_height = height;
    cores3_viewport_t viewport;
    if (!cores3_viewport_init(&viewport, width, height,
                            CORES3_LCD_WIDTH, CORES3_LCD_HEIGHT)) {
        heap_caps_free(video);
        return RAYLIB_LITE_INVALID_ARGUMENT;
    }
    video->view_width = viewport.view_width;
    video->view_height = viewport.view_height;
    video->view_x = viewport.view_x;
    video->view_y = viewport.view_y;
    video->frame = heap_caps_malloc((size_t)width * height * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!video->frame) {
        heap_caps_free(video);
        return RAYLIB_LITE_NO_MEMORY;
    }
    const esp_display_presenter_config_t config = {
        .width = CORES3_LCD_WIDTH, .height = CORES3_LCD_HEIGHT,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = 1, .transfer_timeout_ms = 1500,
        .target = {
            .hw = {
                .panel = panel, .io = io,
                .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .rotation = ESP_DISPLAY_PRESENT_ROTATE_0,
                .swap_bytes = swap_bytes, .te_enabled = false,
                .te_sync = ESP_DISPLAY_PRESENT_TE_SYNC_DISABLED(),
            },
            .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_NONE},
            .drawbuf = {.lines = CORES3_STRIP_ROWS, .buffers = 2, .in_psram = false},
        },
    };
    esp_err_t err = esp_display_presenter_create(&config, &video->presenter);
    if (err != ESP_OK) {
        heap_caps_free(video->frame);
        heap_caps_free(video);
        return result_from_esp(err);
    }
    ESP_LOGI("cores3_video", "Game %ux%u to LCD 320x240, viewport (%u,%u) %ux%u",
             width, height, video->view_x, video->view_y,
             video->view_width, video->view_height);
    *out = video;
    return RAYLIB_LITE_OK;
}

raylib_lite_video_backend_t cores3_video_backend(cores3_video_t *video)
{
    return (raylib_lite_video_backend_t) {
        .context = video,
        .get_info = get_info,
        .acquire = acquire,
        .present = present,
        .discard = discard,
        .flush = flush,
        /* No coherent async snapshot is advertised. */
        .copy_latest = NULL,
        .in_flight = NULL,
    };
}

bool cores3_video_map_touch(const cores3_video_t *video, int32_t *x, int32_t *y)
{
    if (!video) return false;
    cores3_viewport_t viewport = {
        .logical_width = video->logical_width,
        .logical_height = video->logical_height,
        .lcd_width = CORES3_LCD_WIDTH,
        .lcd_height = CORES3_LCD_HEIGHT,
        .view_x = video->view_x,
        .view_y = video->view_y,
        .view_width = video->view_width,
        .view_height = video->view_height,
    };
    return cores3_viewport_map_touch(&viewport, x, y);
}

raylib_lite_result_t cores3_video_close(cores3_video_t *video, uint32_t timeout_ms)
{
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    /* Presenter retains DMA storage on timeout; keep the Board alive for retry. */
    video->acquired = false;
    esp_err_t err = esp_display_presenter_quiesce(video->presenter, timeout_ms);
    if (err != ESP_OK) return result_from_esp(err);
    err = esp_display_presenter_delete(video->presenter);
    if (err != ESP_OK) return result_from_esp(err);
    heap_caps_free(video->frame);
    heap_caps_free(video);
    return RAYLIB_LITE_OK;
}
