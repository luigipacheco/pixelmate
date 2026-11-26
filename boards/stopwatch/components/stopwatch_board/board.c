// SPDX-License-Identifier: Apache-2.0
/* M5Stack StopWatch application-side adapter for Raylib Lite Engine.
 * No ESP Board Manager: the board powers its display and touch itself
 * (sw_power.c, M5's sequence), drives the CO5300 over QSPI (sw_video.c) and
 * polls the CST820 touch (sw_touch.c); sound is sw_audio.c. */
#include "raylib_lite_example_board.h"

#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform_esp_audio.h"
#include "sw_power.h"
#include "sw_touch.h"
#include "sw_video.h"

#define INPUT_CAPACITY 32U
#define POLL_MS 12U
#define TAG "stopwatch_board"

struct raylib_lite_example_board {
    raylib_lite_example_board_config_t config;
    raylib_lite_platform_t services;
    raylib_lite_input_queue_t input;
    raylib_lite_input_event_t events[INPUT_CAPACITY];
    sw_video_t *video;
    bool touch_ok;
    SemaphoreHandle_t input_mutex;
    SemaphoreHandle_t touch_done;
    TaskHandle_t touch_task;
    atomic_bool sampling;
};

static struct raylib_lite_example_board s_board;
static bool s_claimed;

static esp_err_t to_esp(raylib_lite_result_t result)
{
    switch (result) {
    case RAYLIB_LITE_OK: return ESP_OK;
    case RAYLIB_LITE_INVALID_ARGUMENT: return ESP_ERR_INVALID_ARG;
    case RAYLIB_LITE_INVALID_STATE: return ESP_ERR_INVALID_STATE;
    case RAYLIB_LITE_NO_MEMORY: return ESP_ERR_NO_MEM;
    case RAYLIB_LITE_NOT_SUPPORTED: return ESP_ERR_NOT_SUPPORTED;
    case RAYLIB_LITE_BUSY: return ESP_ERR_INVALID_STATE;
    case RAYLIB_LITE_TIMEOUT: return ESP_ERR_TIMEOUT;
    default: return ESP_FAIL;
    }
}

esp_err_t raylib_lite_example_board_haptic_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;   /* Vibration motor (IOE1 PWM) not wired up yet. */
}

esp_err_t raylib_lite_example_board_haptic_set(uint8_t strength)
{
    (void)strength;
    return ESP_ERR_NOT_SUPPORTED;
}

static void lock_input(void *ctx)
{
    (void)xSemaphoreTake((SemaphoreHandle_t)ctx, portMAX_DELAY);
}

static void unlock_input(void *ctx)
{
    (void)xSemaphoreGive((SemaphoreHandle_t)ctx);
}

static uint64_t monotonic_us(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

static void sleep_for_us(void *ctx, uint64_t us)
{
    (void)ctx;
    if (us == 0) {
        taskYIELD();
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS((us + 999U) / 1000U);
    vTaskDelay(ticks ? ticks : 1);
}

/* One finger as the game's pointer. A release the queue cannot take is
 * retried on the next poll. */
static void touch_worker(void *arg)
{
    struct raylib_lite_example_board *board = arg;
    bool was_pressed = false;
    int32_t last_x = 0, last_y = 0;
    while (atomic_load_explicit(&board->sampling, memory_order_acquire)) {
        int32_t x = last_x, y = last_y;
        bool pressed = sw_touch_read(&x, &y);
        if (pressed) {
            pressed = sw_video_map_touch(&x, &y);
            if (!was_pressed) {
                ESP_LOGI(TAG, "touch -> game %ld,%ld", (long)x, (long)y);
            }
        }
        if (!pressed) {
            x = last_x;
            y = last_y;
        }
        if ((pressed && (!was_pressed || x != last_x || y != last_y)) || (!pressed && was_pressed)) {
            const raylib_lite_input_event_t event = {
                .type = RAYLIB_LITE_INPUT_POINTER,
                .x = x, .y = y, .pressed = pressed,
                .timestamp_us = monotonic_us(NULL),
            };
            if (raylib_lite_input_push(&board->input, &event) == RAYLIB_LITE_OK) {
                was_pressed = pressed;
                last_x = x;
                last_y = y;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    xSemaphoreGive(board->touch_done);
    vTaskDelete(NULL);
}

esp_err_t raylib_lite_example_board_create(const raylib_lite_example_board_config_t *config,
                                           raylib_lite_example_board_t **out_board)
{
    if (!config || !out_board || !config->logical_width || !config->logical_height)
        return ESP_ERR_INVALID_ARG;
    if (config->logical_width != 480 || config->logical_height != 480) {
        ESP_LOGE(TAG, "the StopWatch shows a 480x480 game 1:1, not %ux%u",
                 config->logical_width, config->logical_height);
        return ESP_ERR_NOT_SUPPORTED;
    }
    *out_board = NULL;
    if (s_claimed) return ESP_ERR_INVALID_STATE;
    s_claimed = true;
    struct raylib_lite_example_board *board = &s_board;
    memset(board, 0, sizeof(*board));
    atomic_init(&board->sampling, false);
    board->config = *config;

    esp_err_t err = sw_power_init();
    if (err != ESP_OK) return err;
    raylib_lite_result_t result = sw_video_open(&board->video);
    if (result != RAYLIB_LITE_OK) return to_esp(result);
    board->touch_ok = sw_touch_init() == ESP_OK;   /* The pet still shows without touch. */

    board->input_mutex = xSemaphoreCreateMutex();
    board->touch_done = xSemaphoreCreateBinary();
    if (!board->input_mutex || !board->touch_done) return ESP_ERR_NO_MEM;
    const raylib_lite_input_sync_t sync = {
        .context = board->input_mutex, .lock = lock_input, .unlock = unlock_input,
    };
    result = raylib_lite_input_queue_init(&board->input, board->events, INPUT_CAPACITY, &sync);
    if (result != RAYLIB_LITE_OK) return to_esp(result);
    board->services = (raylib_lite_platform_t) {
        .video = sw_video_backend(board->video),
        .clock = {.monotonic_us = monotonic_us, .sleep_for_us = sleep_for_us},
        .audio = NULL,
    };
    if (config->enable_imu) ESP_LOGW(TAG, "IMU input not wired up yet");
    *out_board = board;
    return ESP_OK;
}

/* Engine 0.1.1+ asks for this; it is only used by adaptive games. PixelMate
 * draws a fixed 480 px picture that sw_video centres on the round glass. */
void raylib_lite_example_board_panel_size(uint16_t *width, uint16_t *height)
{
    if (width) *width = 480;
    if (height) *height = 480;
}

const raylib_lite_platform_t *raylib_lite_example_board_services(raylib_lite_example_board_t *board)
{
    return board ? &board->services : NULL;
}

raylib_lite_input_queue_t *raylib_lite_example_board_input(raylib_lite_example_board_t *board)
{
    return board ? &board->input : NULL;
}

esp_err_t raylib_lite_example_board_start_input(raylib_lite_example_board_t *board)
{
    if (!board || atomic_load(&board->sampling)) return ESP_ERR_INVALID_STATE;
    if (!board->touch_ok) return ESP_OK;
    atomic_store(&board->sampling, true);
    if (xTaskCreate(touch_worker, "sw_touch", 4096, board, 5, &board->touch_task) != pdPASS) {
        atomic_store(&board->sampling, false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t raylib_lite_example_board_stop(raylib_lite_example_board_t *board, uint32_t timeout_ms)
{
    if (!board) return ESP_ERR_INVALID_ARG;
    atomic_store(&board->sampling, false);
    if (board->touch_task) {
        if (xSemaphoreTake(board->touch_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return ESP_ERR_TIMEOUT;
        board->touch_task = NULL;
    }
    return ESP_OK;
}

esp_err_t raylib_lite_example_board_retry_cleanup(raylib_lite_example_board_t *board, uint32_t timeout_ms)
{
    if (!board || board != &s_board) return ESP_ERR_INVALID_ARG;
    raylib_lite_result_t audio = raylib_lite_game_audio_shutdown(timeout_ms);
    if (audio != RAYLIB_LITE_OK) return to_esp(audio);
    esp_err_t err = raylib_lite_example_board_stop(board, timeout_ms);
    if (err != ESP_OK) return err;
    if (board->video) {
        raylib_lite_result_t result = sw_video_close(board->video, timeout_ms);
        if (result != RAYLIB_LITE_OK) return to_esp(result);
        board->video = NULL;
    }
    if (board->input.initialized) raylib_lite_input_queue_deinit(&board->input);
    if (board->touch_done) {
        vSemaphoreDelete(board->touch_done);
        board->touch_done = NULL;
    }
    if (board->input_mutex) {
        vSemaphoreDelete(board->input_mutex);
        board->input_mutex = NULL;
    }
    s_claimed = false;
    return ESP_OK;
}

esp_err_t raylib_lite_example_board_destroy(raylib_lite_example_board_t *board, uint32_t timeout_ms)
{
    return raylib_lite_example_board_retry_cleanup(board, timeout_ms);
}
