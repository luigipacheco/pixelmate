// SPDX-License-Identifier: Apache-2.0
/* Face finder for the M5Stack CoreS3 (StackChan): Espressif's ESP-DL face
 * detector (two-stage MSR+MNP model, ~40 ms a run on the ESP32-S3) on the
 * camera's 128x128 feed. A low-priority worker on CPU 0 runs it a few times a
 * second while enabled; the game loop (CPU 1) only takes the latest result.
 * Frames are converted and dropped in memory: nothing is stored or sent. */
#include <atomic>
#include <list>
#include "app_camera_hw.h"
#include "app_face_hw.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"

namespace {

const char *TAG = "face";

constexpr int SIDE = APP_PHOTO_MAX_SIDE;          /* The camera feed is SIDE x SIDE. */
constexpr int PERIOD_MS = 300;                   /* Between detections. */
constexpr int WAIT_MS = 100;                     /* No new camera frame yet. */
constexpr int TASK_STACK = 8192;
constexpr UBaseType_t TASK_PRIORITY = 1;
constexpr BaseType_t TASK_CORE = 0;              /* The game loop runs on CPU 1. */
constexpr float MIN_SCORE = 0.5f;
constexpr int REPORT_MS = 10000;                 /* Detection statistics in the log. */
constexpr int PICTURE_MS = 60000;                /* Text picture of the camera view in the log. */
/* How the feed maps to the world. With both false: +x in the result is the
 * image's right, +y is down. Flip here if the head turns away from faces. */
constexpr bool MIRROR_X = false;
constexpr bool FLIP_Y = false;

std::atomic<bool> s_on{false};
TaskHandle_t s_task;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/* Latest result, under s_lock. */
uint32_t s_seq, s_taken;
bool s_seen;
float s_x, s_y;

uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* The camera's Y, U, V planes (U/V centered on 128) to packed RGB888. */
void to_rgb(const uint8_t *yuv, uint8_t *rgb)
{
    const uint8_t *py = yuv, *pu = yuv + SIDE * SIDE, *pv = yuv + 2 * SIDE * SIDE;
    for (int i = 0; i < SIDE * SIDE; ++i) {
        const int y = py[i], u = pu[i] - 128, v = pv[i] - 128;
        rgb[3 * i + 0] = clamp8(y + (1436 * v) / 1024);
        rgb[3 * i + 1] = clamp8(y - (352 * u + 731 * v) / 1024);
        rgb[3 * i + 2] = clamp8(y + (1815 * u) / 1024);
    }
}

void publish(bool seen, float x, float y)
{
    taskENTER_CRITICAL(&s_lock);
    s_seen = seen;
    s_x = x;
    s_y = y;
    ++s_seq;
    taskEXIT_CRITICAL(&s_lock);
}

/* Diagnostics: a coarse picture of what the camera sees, as text in the log
 * (brighter = denser character), to check exposure and orientation. */
void log_thumbnail(const uint8_t *luma)
{
    static const char RAMP[] = " .:-=+*#%@";
    constexpr int COLS = 32, ROWS = 16;
    char line[COLS + 1];
    ESP_LOGI(TAG, "camera view (top of the image first):");
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            const int v = luma[(r * SIDE / ROWS + SIDE / ROWS / 2) * SIDE + c * SIDE / COLS + SIDE / COLS / 2];
            line[c] = RAMP[v * (int)(sizeof(RAMP) - 2) / 255];
        }
        line[COLS] = 0;
        ESP_LOGI(TAG, "|%s|", line);
    }
}

void worker(void *)
{
    uint8_t *yuv = static_cast<uint8_t *>(heap_caps_malloc(APP_CAMERA_FRAME_BYTES, MALLOC_CAP_SPIRAM));
    uint8_t *rgb = static_cast<uint8_t *>(heap_caps_malloc(SIDE * SIDE * 3, MALLOC_CAP_SPIRAM));
    if (!yuv || !rgb) {
        ESP_LOGE(TAG, "no memory for frames");
        vTaskDelete(nullptr);
        return;
    }
    HumanFaceDetect *detect = nullptr;
    uint32_t frame_seq = 0;
    bool was_seen = false;
    uint32_t runs = 0, hits = 0, luma_sum = 0, run_us = 0;
    TickType_t report_at = xTaskGetTickCount() + pdMS_TO_TICKS(REPORT_MS);
    TickType_t picture_at = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    for (;;) {
        if (!s_on.load()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (app_camera_hw_state() != APP_CAMERA_LIVE || !app_camera_hw_frame(yuv, &frame_seq)) {
            vTaskDelay(pdMS_TO_TICKS(WAIT_MS));
            continue;
        }
        if (!detect) {
            detect = new HumanFaceDetect();   /* Loads the model from flash on first use. */
            ESP_LOGI(TAG, "face detector ready");
        }
        to_rgb(yuv, rgb);
        const dl::image::img_t img = {rgb, SIDE, SIDE, dl::image::DL_IMAGE_PIX_TYPE_RGB888};
        const int64_t started = esp_timer_get_time();
        std::list<dl::detect::result_t> &found = detect->run(img);
        run_us += (uint32_t)(esp_timer_get_time() - started);
        ++runs;
        luma_sum += yuv[(SIDE / 2) * SIDE + SIDE / 2] + yuv[(SIDE / 4) * SIDE + SIDE / 4] + yuv[(3 * SIDE / 4) * SIDE + 3 * SIDE / 4];
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - picture_at) >= 0) {
            log_thumbnail(yuv);
            picture_at = now + pdMS_TO_TICKS(PICTURE_MS);
        }
        const dl::detect::result_t *best = nullptr;
        for (const auto &r : found) {
            if (r.score >= MIN_SCORE && r.box.size() >= 4 && (!best || r.box_area() > best->box_area())) {
                best = &r;
            }
        }
        if (best) {
            float x = ((best->box[0] + best->box[2]) * 0.5f) / (SIDE / 2) - 1.0f;
            float y = ((best->box[1] + best->box[3]) * 0.5f) / (SIDE / 2) - 1.0f;
            x = MIRROR_X ? -x : x;
            y = FLIP_Y ? -y : y;
            publish(true, x, y);
            if (!was_seen) {
                ESP_LOGI(TAG, "face at x %.2f y %.2f (score %.2f, %d px wide)", x, y, best->score,
                         best->box[2] - best->box[0]);
            }
        } else {
            publish(false, 0.0f, 0.0f);
            if (was_seen) {
                ESP_LOGI(TAG, "face gone");
            }
        }
        was_seen = best != nullptr;
        hits += best != nullptr;
        if ((int32_t)(now - report_at) >= 0) {
            ESP_LOGI(TAG, "%lu runs (%lu ms each), %lu with a face, brightness %lu", (unsigned long)runs,
                     (unsigned long)(runs ? run_us / runs / 1000 : 0), (unsigned long)hits,
                     (unsigned long)(runs ? luma_sum / runs / 3 : 0));
            runs = hits = luma_sum = run_us = 0;
            report_at = now + pdMS_TO_TICKS(REPORT_MS);
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

}  // namespace

void app_face_hw_enable(bool on)
{
    if (s_on.exchange(on) == on) {
        return;
    }
    if (on && !s_task
            && xTaskCreatePinnedToCore(worker, "face", TASK_STACK, nullptr, TASK_PRIORITY, &s_task, TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the face task");
        s_task = nullptr;
        s_on.store(false);
        return;
    }
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

bool app_face_hw_take(bool *seen, float *x, float *y)
{
    bool fresh = false;
    taskENTER_CRITICAL(&s_lock);
    if (s_seq != s_taken) {
        s_taken = s_seq;
        *seen = s_seen;
        *x = s_x;
        *y = s_y;
        fresh = true;
    }
    taskEXIT_CRITICAL(&s_lock);
    return fresh;
}
