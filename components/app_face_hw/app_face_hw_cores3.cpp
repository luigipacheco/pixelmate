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
        std::list<dl::detect::result_t> &found = detect->run(img);
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
