// SPDX-License-Identifier: Apache-2.0
/* SPI LCD presenter for a virtual Game surface with centered letterboxing.
 *
 * Two logical frames live in PSRAM. The game draws into one while a sender
 * task (CPU 0) pushes the other to the panel, so drawing (~37 ms) and the SPI
 * transfer (~30 ms) overlap instead of adding up. present() only queues the
 * frame; acquire() waits for a frame the sender has finished with.
 *
 * The first frame covers the whole panel, which paints the letterbox bars
 * black; later frames send only the viewport, a third less SPI traffic.
 * esp_display_present owns DMA buffers, callbacks and transfer fencing and
 * is used from the sender task only. */
#include "cores3_video.h"
#include "cores3_viewport.h"
#include "cores3_strip.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_display_present.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CORES3_LCD_WIDTH 320U
#define CORES3_LCD_HEIGHT 240U
/* The CoreS3 profile's SPI bus max_transfer_sz is 3600 bytes: 5 rows of
 * 320 RGB565 pixels are 3200 bytes. */
#define CORES3_STRIP_ROWS 5U
#define CORES3_FRAME_TOKEN ((uintptr_t)0xB0330U)
#define CORES3_FRAMES 2U
#define CORES3_ACQUIRE_WAIT_MS 200U
#define CORES3_SENDER_STACK 4096U
#define CORES3_SENDER_PRIORITY 6U
#define CORES3_STOP 0xFFU

static const char *TAG = "cores3_video";

struct cores3_video {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    cores3_viewport_t view;
    uint16_t *frames[CORES3_FRAMES];
    bool busy[CORES3_FRAMES];         /* Acquired or queued: not free to draw. */
    int acquired;                     /* Frame the game holds, -1 = none. */
    portMUX_TYPE lock;
    SemaphoreHandle_t free_frames;    /* Counts frames free to acquire. */
    QueueHandle_t queue;              /* Frame indexes to send, in order. */
    TaskHandle_t sender;
    SemaphoreHandle_t sender_done;    /* Given when the sender exits. */
    esp_display_presenter_t *presenter;
    volatile uint32_t in_flight;
    bool bars_painted;
    volatile bool failed;
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
        .width = video->view.logical_width,
        .height = video->view.logical_height,
        .stride_pixels = video->view.logical_width,
        .format = RAYLIB_LITE_PIXEL_RGB565_NATIVE,
    };
    return RAYLIB_LITE_OK;
}

/* The camera's DMA writes its frames into PSRAM too. With the game drawing
 * on one core while the sender reads on the other, it loses every frame, so
 * while the camera streams the two take turns (the camera driver overrides
 * this). */
bool stackchan_camera_streaming(void);
__attribute__((weak)) bool stackchan_camera_streaming(void)
{
    return false;
}

static raylib_lite_result_t wait_idle(cores3_video_t *video, uint32_t timeout_ms);

static void release_frame(cores3_video_t *video, int index)
{
    portENTER_CRITICAL(&video->lock);
    video->busy[index] = false;
    portEXIT_CRITICAL(&video->lock);
    xSemaphoreGive(video->free_frames);
}

static raylib_lite_result_t acquire(void *ctx, raylib_lite_frame_t *out)
{
    cores3_video_t *video = ctx;
    if (!video || !out) return RAYLIB_LITE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (video->failed) return RAYLIB_LITE_PLATFORM_ERROR;
    if (video->acquired >= 0) return RAYLIB_LITE_INVALID_STATE;
    /* Waits while both frames are queued or sending: the game then runs at
     * the panel's pace instead of drawing frames nobody will see. */
    if (stackchan_camera_streaming() && wait_idle(video, CORES3_ACQUIRE_WAIT_MS) != RAYLIB_LITE_OK)
        return RAYLIB_LITE_BUSY;   /* Take turns with the camera's DMA. */
    if (xSemaphoreTake(video->free_frames, pdMS_TO_TICKS(CORES3_ACQUIRE_WAIT_MS)) != pdTRUE)
        return RAYLIB_LITE_BUSY;
    int index = -1;
    portENTER_CRITICAL(&video->lock);
    for (int i = 0; i < (int)CORES3_FRAMES; ++i) {
        if (!video->busy[i]) {
            video->busy[i] = true;
            index = i;
            break;
        }
    }
    portEXIT_CRITICAL(&video->lock);
    if (index < 0) {
        xSemaphoreGive(video->free_frames);
        return RAYLIB_LITE_BUSY;
    }
    video->acquired = index;
    *out = (raylib_lite_frame_t) {
        .pixels = video->frames[index],
        .width = video->view.logical_width,
        .height = video->view.logical_height,
        .stride_pixels = video->view.logical_width,
        .token = CORES3_FRAME_TOKEN + (uintptr_t)index,
    };
    return RAYLIB_LITE_OK;
}

/* Push one logical frame to the panel. Sender task only. */
static esp_err_t send_frame(cores3_video_t *video, const uint16_t *frame)
{
    const bool whole = !video->bars_painted;
    const uint16_t x0 = whole ? 0 : video->view.view_x;
    const uint16_t y0 = whole ? 0 : video->view.view_y;
    const uint16_t width = whole ? CORES3_LCD_WIDTH : video->view.view_width;
    const uint16_t height = whole ? CORES3_LCD_HEIGHT : video->view.view_height;
    const size_t stride = (size_t)width * sizeof(uint16_t);

    size_t areas = 0;
    bool full = false;
    esp_err_t err = esp_display_presenter_begin_next_frame(
        video->presenter, NULL, NULL, 0, &areas, &full);
    if (err != ESP_OK) return err;
    for (uint16_t y = 0; y < height;) {
        esp_display_presenter_buffer_t lease = {0};
        err = esp_display_presenter_acquire_buffer(video->presenter, &lease);
        if (err != ESP_OK) goto cancel;
        size_t rows = 0;
        const size_t remaining = height - y;
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
        if (whole) {
            cores3_fill_strip_pixels(&video->view, frame, lease.surface.pixels, y, (uint16_t)rows, false);
        } else {
            cores3_fill_view_rows(&video->view, frame, lease.surface.pixels, y, (uint16_t)rows);
        }
        const esp_display_present_area_t area = {
            .x1 = x0, .y1 = y0 + y,
            .x2 = x0 + width - 1, .y2 = y0 + y + rows - 1,
        };
        err = esp_display_presenter_submit_buffer(video->presenter, &lease, &area, stride);
        if (err != ESP_OK) goto cancel;
        y += (uint16_t)rows;
    }
    const esp_display_present_area_t covered = {
        .x1 = x0, .y1 = y0, .x2 = x0 + width - 1, .y2 = y0 + height - 1,
    };
    const esp_display_presenter_submit_t done = whole
        ? (esp_display_presenter_submit_t) {.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL}
        : (esp_display_presenter_submit_t) {
            .coverage = ESP_DISPLAY_PRESENT_COVERAGE_AREAS, .areas = &covered, .area_count = 1,
        };
    err = esp_display_presenter_commit_frame(video->presenter, &done);
    if (err != ESP_OK) goto cancel;
    err = esp_display_presenter_quiesce(video->presenter, 1500);
    if (err == ESP_OK) video->bars_painted = true;
    return err;
cancel:
    esp_display_presenter_cancel_frame(video->presenter);
    return err;
}

static void sender(void *arg)
{
    cores3_video_t *video = arg;
    for (;;) {
        uint8_t index = 0;
        xQueueReceive(video->queue, &index, portMAX_DELAY);
        if (index == CORES3_STOP) break;
        if (!video->failed) {
            const esp_err_t err = send_frame(video, video->frames[index]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "panel transfer: %s", esp_err_to_name(err));
                video->failed = true;
            }
        }
        release_frame(video, index);
        __atomic_sub_fetch(&video->in_flight, 1U, __ATOMIC_SEQ_CST);
    }
    xSemaphoreGive(video->sender_done);
    vTaskDelete(NULL);
}

static bool frame_valid(const cores3_video_t *video, const raylib_lite_frame_t *frame)
{
    return frame && video->acquired >= 0 &&
        frame->pixels == video->frames[video->acquired] &&
        frame->token == CORES3_FRAME_TOKEN + (uintptr_t)video->acquired &&
        frame->width == video->view.logical_width &&
        frame->height == video->view.logical_height &&
        frame->stride_pixels == video->view.logical_width;
}

static raylib_lite_result_t present(void *ctx, raylib_lite_frame_t *frame)
{
    cores3_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    const bool valid = frame_valid(video, frame);
    const int index = video->acquired;
    video->acquired = -1;
    if (frame) memset(frame, 0, sizeof(*frame));
    if (!valid) {
        if (index >= 0) release_frame(video, index);
        return RAYLIB_LITE_INVALID_STATE;
    }
    if (video->failed) {
        release_frame(video, index);
        return RAYLIB_LITE_PLATFORM_ERROR;
    }
    const uint8_t item = (uint8_t)index;
    __atomic_add_fetch(&video->in_flight, 1U, __ATOMIC_SEQ_CST);
    if (xQueueSend(video->queue, &item, 0) != pdTRUE) {   /* Holds every frame: cannot fill. */
        __atomic_sub_fetch(&video->in_flight, 1U, __ATOMIC_SEQ_CST);
        release_frame(video, index);
        return RAYLIB_LITE_BUSY;
    }
    return RAYLIB_LITE_OK;
}

static void discard(void *ctx, raylib_lite_frame_t *frame)
{
    cores3_video_t *video = ctx;
    if (video && video->acquired >= 0) {
        const int index = video->acquired;
        video->acquired = -1;
        release_frame(video, index);
    }
    if (frame) memset(frame, 0, sizeof(*frame));
}

static raylib_lite_result_t wait_idle(cores3_video_t *video, uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    while (__atomic_load_n(&video->in_flight, __ATOMIC_SEQ_CST)) {
        if (timeout_ms != RAYLIB_LITE_WAIT_FOREVER &&
                (xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            return RAYLIB_LITE_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return RAYLIB_LITE_OK;
}

static raylib_lite_result_t flush(void *ctx, uint32_t timeout_ms)
{
    cores3_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    if (video->acquired >= 0) return RAYLIB_LITE_INVALID_STATE;
    return wait_idle(video, timeout_ms);
}

static uint32_t in_flight(void *ctx)
{
    cores3_video_t *video = ctx;
    return video ? __atomic_load_n(&video->in_flight, __ATOMIC_SEQ_CST) : 0;
}

static void free_video(cores3_video_t *video)
{
    for (uint32_t i = 0; i < CORES3_FRAMES; ++i) heap_caps_free(video->frames[i]);
    if (video->free_frames) vSemaphoreDelete(video->free_frames);
    if (video->sender_done) vSemaphoreDelete(video->sender_done);
    if (video->queue) vQueueDelete(video->queue);
    heap_caps_free(video);
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
    video->acquired = -1;
    portMUX_INITIALIZE(&video->lock);
    if (!cores3_viewport_init(&video->view, width, height,
                              CORES3_LCD_WIDTH, CORES3_LCD_HEIGHT)) {
        heap_caps_free(video);
        return RAYLIB_LITE_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < CORES3_FRAMES; ++i) {
        video->frames[i] = heap_caps_calloc((size_t)width * height, sizeof(uint16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    video->free_frames = xSemaphoreCreateCounting(CORES3_FRAMES, CORES3_FRAMES);
    video->sender_done = xSemaphoreCreateBinary();
    video->queue = xQueueCreate(CORES3_FRAMES + 1U, sizeof(uint8_t));   /* + the stop message. */
    if (!video->frames[0] || !video->frames[1] || !video->free_frames || !video->sender_done || !video->queue) {
        free_video(video);
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
        free_video(video);
        return result_from_esp(err);
    }
    /* CPU 0, next to the camera; the game loop draws on CPU 1. */
    if (xTaskCreatePinnedToCore(sender, "cores3_lcd", CORES3_SENDER_STACK, video,
                                CORES3_SENDER_PRIORITY, &video->sender, 0) != pdPASS) {
        esp_display_presenter_delete(video->presenter);
        free_video(video);
        return RAYLIB_LITE_NO_MEMORY;
    }
    ESP_LOGI(TAG, "Game %ux%u to LCD 320x240, viewport (%u,%u) %ux%u, %u frames",
             width, height, video->view.view_x, video->view.view_y,
             video->view.view_width, video->view.view_height, (unsigned)CORES3_FRAMES);
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
        .in_flight = in_flight,
    };
}

bool cores3_video_map_touch(const cores3_video_t *video, int32_t *x, int32_t *y)
{
    if (!video) return false;
    return cores3_viewport_map_touch(&video->view, x, y);
}

raylib_lite_result_t cores3_video_close(cores3_video_t *video, uint32_t timeout_ms)
{
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    if (video->acquired >= 0) discard(video, NULL);
    /* Frames still queued are sent first; on timeout the Board stays alive
     * for a retry. */
    raylib_lite_result_t result = wait_idle(video, timeout_ms);
    if (result != RAYLIB_LITE_OK) return result;
    if (video->sender) {
        const uint8_t stop = CORES3_STOP;
        xQueueSend(video->queue, &stop, portMAX_DELAY);
        xSemaphoreTake(video->sender_done, portMAX_DELAY);
        video->sender = NULL;
    }
    esp_err_t err = esp_display_presenter_rebind_producer(video->presenter);
    if (err == ESP_OK) err = esp_display_presenter_quiesce(video->presenter, timeout_ms);
    if (err != ESP_OK) return result_from_esp(err);
    err = esp_display_presenter_delete(video->presenter);
    if (err != ESP_OK) return result_from_esp(err);
    free_video(video);
    return RAYLIB_LITE_OK;
}
