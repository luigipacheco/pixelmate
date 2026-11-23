// SPDX-License-Identifier: Apache-2.0
/* M5Stack StopWatch display: CO5300 AMOLED over QSPI, driven as a 480x480
 * memory like the ESP-Mosaico's panel (M5GFX's Panel_StopWatch: 480x480
 * memory, the round glass shows the middle 468, 6 columns in). The game's
 * 480x480 surface goes out 1:1.
 *
 * Two logical frames live in PSRAM: the game draws one while a sender task
 * (CPU 0) pushes the other through esp_display_present, which owns the DMA
 * strips and transfer fencing and is used from the sender task only. */
#include "sw_video.h"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_display_present.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sw_power.h"

#define TAG "sw_video"
#define LCD_SIZE 480U
#define LCD_HOST SPI2_HOST
#define LCD_PCLK_HZ (40U * 1000U * 1000U)   /* M5GFX runs 80 MHz; start safe. */
#define STRIP_ROWS 16U
#define FRAMES 2U
#define FRAME_TOKEN ((uintptr_t)0x5A700U)
#define ACQUIRE_WAIT_MS 200U
#define SENDER_STACK 4096U
#define SENDER_PRIORITY 6U
#define STOP_ITEM 0xFFU

/* M5GFX Panel_StopWatch init list (MIT, M5Stack), plus RGB565 (0x3A). */
static const co5300_lcd_init_cmd_t s_init[] = {
    {0x11, NULL, 0, 150},                          /* Sleep out. */
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},               /* 16 bits per pixel. */
    {0x35, (uint8_t[]){0x80}, 1, 0},               /* Tearing effect on. */
    {0x44, (uint8_t[]){0x01, 0xD2}, 2, 0},         /* TE line 466. */
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x20, NULL, 0, 0},                            /* Inversion off. */
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x51, (uint8_t[]){0xA0}, 1, 0},               /* Brightness. */
    {0x29, NULL, 0, 20},                           /* Display on. */
};

struct sw_video {
    uint16_t *frames[FRAMES];
    bool busy[FRAMES];
    int acquired;
    portMUX_TYPE lock;
    SemaphoreHandle_t free_frames;
    QueueHandle_t queue;
    TaskHandle_t sender;
    SemaphoreHandle_t sender_done;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    esp_display_presenter_t *presenter;
    volatile uint32_t in_flight;
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
    if (!ctx || !out) return RAYLIB_LITE_INVALID_ARGUMENT;
    *out = (raylib_lite_video_info_t) {
        .width = LCD_SIZE, .height = LCD_SIZE, .stride_pixels = LCD_SIZE,
        .format = RAYLIB_LITE_PIXEL_RGB565_NATIVE,
    };
    return RAYLIB_LITE_OK;
}

static void release_frame(sw_video_t *video, int index)
{
    portENTER_CRITICAL(&video->lock);
    video->busy[index] = false;
    portEXIT_CRITICAL(&video->lock);
    xSemaphoreGive(video->free_frames);
}

static raylib_lite_result_t acquire(void *ctx, raylib_lite_frame_t *out)
{
    sw_video_t *video = ctx;
    if (!video || !out) return RAYLIB_LITE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (video->failed) return RAYLIB_LITE_PLATFORM_ERROR;
    if (video->acquired >= 0) return RAYLIB_LITE_INVALID_STATE;
    if (xSemaphoreTake(video->free_frames, pdMS_TO_TICKS(ACQUIRE_WAIT_MS)) != pdTRUE)
        return RAYLIB_LITE_BUSY;
    int index = -1;
    portENTER_CRITICAL(&video->lock);
    for (int i = 0; i < (int)FRAMES; ++i) {
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
        .width = LCD_SIZE, .height = LCD_SIZE, .stride_pixels = LCD_SIZE,
        .token = FRAME_TOKEN + (uintptr_t)index,
    };
    return RAYLIB_LITE_OK;
}

/* One frame to the panel in strips. Sender task only. */
static esp_err_t send_frame(sw_video_t *video, const uint16_t *frame)
{
    const size_t stride = LCD_SIZE * sizeof(uint16_t);
    size_t areas = 0;
    bool full = false;
    esp_err_t err = esp_display_presenter_begin_next_frame(video->presenter, NULL, NULL, 0, &areas, &full);
    if (err != ESP_OK) return err;
    for (uint16_t y = 0; y < LCD_SIZE;) {
        esp_display_presenter_buffer_t lease = {0};
        err = esp_display_presenter_acquire_buffer(video->presenter, &lease);
        if (err != ESP_OK) goto cancel;
        size_t rows = 0;
        const size_t remaining = LCD_SIZE - y;
        if (!lease.lease_id || !lease.surface.pixels || !lease.resolve_rows || lease.capacity_bytes < stride) {
            err = ESP_FAIL;
            goto cancel;
        }
        err = lease.resolve_rows(lease.resolve_rows_ctx, lease.lease_id, stride, remaining, &rows);
        if (err != ESP_OK) goto cancel;
        if (!rows || rows > remaining || rows > lease.capacity_bytes / stride) {
            err = ESP_FAIL;
            goto cancel;
        }
        memcpy(lease.surface.pixels, frame + (size_t)y * LCD_SIZE, rows * stride);
        const esp_display_present_area_t area = {
            .x1 = 0, .y1 = y, .x2 = LCD_SIZE - 1, .y2 = y + rows - 1,
        };
        err = esp_display_presenter_submit_buffer(video->presenter, &lease, &area, stride);
        if (err != ESP_OK) goto cancel;
        y += (uint16_t)rows;
    }
    const esp_display_presenter_submit_t done = {.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL};
    err = esp_display_presenter_commit_frame(video->presenter, &done);
    if (err != ESP_OK) goto cancel;
    return esp_display_presenter_quiesce(video->presenter, 1500);
cancel:
    esp_display_presenter_cancel_frame(video->presenter);
    return err;
}

static void sender(void *arg)
{
    sw_video_t *video = arg;
    for (;;) {
        uint8_t index = 0;
        xQueueReceive(video->queue, &index, portMAX_DELAY);
        if (index == STOP_ITEM) break;
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

static raylib_lite_result_t present(void *ctx, raylib_lite_frame_t *frame)
{
    sw_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    const int index = video->acquired;
    const bool valid = frame && index >= 0 && frame->pixels == video->frames[index] &&
        frame->token == FRAME_TOKEN + (uintptr_t)index &&
        frame->width == LCD_SIZE && frame->height == LCD_SIZE && frame->stride_pixels == LCD_SIZE;
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
    if (xQueueSend(video->queue, &item, 0) != pdTRUE) {
        __atomic_sub_fetch(&video->in_flight, 1U, __ATOMIC_SEQ_CST);
        release_frame(video, index);
        return RAYLIB_LITE_BUSY;
    }
    return RAYLIB_LITE_OK;
}

static void discard(void *ctx, raylib_lite_frame_t *frame)
{
    sw_video_t *video = ctx;
    if (video && video->acquired >= 0) {
        const int index = video->acquired;
        video->acquired = -1;
        release_frame(video, index);
    }
    if (frame) memset(frame, 0, sizeof(*frame));
}

static raylib_lite_result_t wait_idle(sw_video_t *video, uint32_t timeout_ms)
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
    sw_video_t *video = ctx;
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    if (video->acquired >= 0) return RAYLIB_LITE_INVALID_STATE;
    return wait_idle(video, timeout_ms);
}

static uint32_t in_flight(void *ctx)
{
    sw_video_t *video = ctx;
    return video ? __atomic_load_n(&video->in_flight, __ATOMIC_SEQ_CST) : 0;
}

static esp_err_t open_panel(sw_video_t *video)
{
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        SW_LCD_SCLK, SW_LCD_D0, SW_LCD_D1, SW_LCD_D2, SW_LCD_D3, LCD_SIZE * STRIP_ROWS * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");
    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(SW_LCD_CS, NULL, NULL);
    io_config.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &video->io),
                        TAG, "panel IO");
    const co5300_vendor_config_t vendor = {
        .init_cmds = s_init,
        .init_cmds_size = sizeof(s_init) / sizeof(s_init[0]),
        .flags.use_qspi_interface = true,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,   /* Reset through the IOE1, done in sw_power_init(). */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(video->io, &panel_config, &video->panel), TAG, "CO5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(video->panel), TAG, "CO5300 init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(video->panel, true), TAG, "display on");
    return ESP_OK;
}

raylib_lite_result_t sw_video_open(sw_video_t **out)
{
    if (!out) return RAYLIB_LITE_INVALID_ARGUMENT;
    *out = NULL;
    sw_video_t *video = heap_caps_calloc(1, sizeof(*video), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!video) return RAYLIB_LITE_NO_MEMORY;
    video->acquired = -1;
    portMUX_INITIALIZE(&video->lock);
    for (uint32_t i = 0; i < FRAMES; ++i) {
        video->frames[i] = heap_caps_calloc((size_t)LCD_SIZE * LCD_SIZE, sizeof(uint16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    video->free_frames = xSemaphoreCreateCounting(FRAMES, FRAMES);
    video->sender_done = xSemaphoreCreateBinary();
    video->queue = xQueueCreate(FRAMES + 1U, sizeof(uint8_t));
    if (!video->frames[0] || !video->frames[1] || !video->free_frames || !video->sender_done || !video->queue) {
        ESP_LOGE(TAG, "no memory for frames");
        return RAYLIB_LITE_NO_MEMORY;   /* Boot-time only: not worth unwinding. */
    }
    esp_err_t err = open_panel(video);
    if (err != ESP_OK) return result_from_esp(err);
    const esp_display_presenter_config_t config = {
        .width = LCD_SIZE, .height = LCD_SIZE,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = 1, .transfer_timeout_ms = 1500,
        .target = {
            .hw = {
                .panel = video->panel, .io = video->io,
                .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .rotation = ESP_DISPLAY_PRESENT_ROTATE_0,
                .swap_bytes = true, .te_enabled = false,
                .te_sync = ESP_DISPLAY_PRESENT_TE_SYNC_DISABLED(),
            },
            .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_NONE},
            .drawbuf = {.lines = STRIP_ROWS, .buffers = 2, .in_psram = false},
        },
    };
    err = esp_display_presenter_create(&config, &video->presenter);
    if (err != ESP_OK) return result_from_esp(err);
    if (xTaskCreatePinnedToCore(sender, "sw_lcd", SENDER_STACK, video, SENDER_PRIORITY, &video->sender, 0) != pdPASS)
        return RAYLIB_LITE_NO_MEMORY;
    ESP_LOGI(TAG, "CO5300 480x480 over QSPI at %u MHz, %u frames", (unsigned)(LCD_PCLK_HZ / 1000000U),
             (unsigned)FRAMES);
    *out = video;
    return RAYLIB_LITE_OK;
}

raylib_lite_video_backend_t sw_video_backend(sw_video_t *video)
{
    return (raylib_lite_video_backend_t) {
        .context = video,
        .get_info = get_info,
        .acquire = acquire,
        .present = present,
        .discard = discard,
        .flush = flush,
        .copy_latest = NULL,
        .in_flight = in_flight,
    };
}

raylib_lite_result_t sw_video_close(sw_video_t *video, uint32_t timeout_ms)
{
    if (!video) return RAYLIB_LITE_INVALID_ARGUMENT;
    if (video->acquired >= 0) discard(video, NULL);
    raylib_lite_result_t result = wait_idle(video, timeout_ms);
    if (result != RAYLIB_LITE_OK) return result;
    if (video->sender) {
        const uint8_t stop = STOP_ITEM;
        xQueueSend(video->queue, &stop, portMAX_DELAY);
        xSemaphoreTake(video->sender_done, portMAX_DELAY);
        video->sender = NULL;
    }
    esp_err_t err = esp_display_presenter_rebind_producer(video->presenter);
    if (err == ESP_OK) err = esp_display_presenter_quiesce(video->presenter, timeout_ms);
    if (err == ESP_OK) err = esp_display_presenter_delete(video->presenter);
    return result_from_esp(err);
}
