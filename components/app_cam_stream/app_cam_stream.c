// SPDX-License-Identifier: Apache-2.0
/* Booth webcam over Wi-Fi: its own HTTP server on port 81, running only while
 * the booth is ON AIR. GET /cam?k=<code> streams MJPEG; GET /cam.jpg?k=<code>
 * returns one frame. Frames come from app_camera_hw (hardware JPEG). */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_cam_stream.h"
#include "app_camera_hw.h"
#include "app_net.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cam_stream";

enum {
    PORT = 81,
    CTRL_PORT = 32771,          /* Distinct from the stats server's control port. */
    SOCKETS = 2,                /* One viewer (the stream holds the server task). */
    QUERY_MAX = 24,
    POLL_MS = 15,
    FRAME_WAIT_MS = 1500,       /* No new frame for this long ends the stream. */
};
#define BOUNDARY "pixelmateframe"

static httpd_handle_t s_server;
static _Atomic bool s_on;
static _Atomic int s_code;

static bool key_ok(httpd_req_t *req)
{
    char query[QUERY_MAX], value[8];
    if (!atomic_load(&s_on) || httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK
            || httpd_query_key_value(query, "k", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    return atoi(value) == atomic_load(&s_code);
}

/* Waits for a frame newer than *seq while the stream stays on with the same code. */
static size_t next_frame(uint8_t *buf, uint32_t *seq, int code)
{
    for (int waited = 0; waited < FRAME_WAIT_MS; waited += POLL_MS) {
        if (!atomic_load(&s_on) || atomic_load(&s_code) != code) {
            return 0;
        }
        const size_t len = app_camera_hw_jpeg(buf, APP_CAMERA_JPEG_MAX, seq);
        if (len) {
            return len;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

static esp_err_t get_jpg(httpd_req_t *req)
{
    if (!key_ok(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "wrong code");
    }
    uint8_t *buf = heap_caps_malloc(APP_CAMERA_JPEG_MAX, MALLOC_CAP_SPIRAM);
    uint32_t seq = 0;
    const size_t len = buf ? next_frame(buf, &seq, atomic_load(&s_code)) : 0;
    esp_err_t err;
    if (!len) {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no frame");
    } else {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_send(req, (const char *)buf, (ssize_t)len);
    }
    free(buf);
    return err;
}

static esp_err_t get_stream(httpd_req_t *req)
{
    if (!key_ok(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "wrong code");
    }
    uint8_t *buf = heap_caps_malloc(APP_CAMERA_JPEG_MAX, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const int code = atomic_load(&s_code);
    uint32_t seq = 0;
    char head[96];
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "viewer connected");
    for (size_t len; err == ESP_OK && (len = next_frame(buf, &seq, code)) > 0;) {
        const int n = snprintf(head, sizeof(head),
                               "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                               (unsigned)len);
        err = httpd_resp_send_chunk(req, head, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, (const char *)buf, (ssize_t)len);
        }
    }
    free(buf);
    ESP_LOGI(TAG, "viewer left");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static void start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = PORT;
    config.ctrl_port = CTRL_PORT;
    config.max_open_sockets = SOCKETS;
    config.lru_purge_enable = true;
    config.stack_size = 4096;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGW(TAG, "server start failed");
        s_server = NULL;
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/cam", .method = HTTP_GET, .handler = get_stream},
        {.uri = "/cam.jpg", .method = HTTP_GET, .handler = get_jpg},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "on air: http://<board>:%d/cam?k=%d", PORT, atomic_load(&s_code));
}

void app_cam_stream_set(bool on_air, int code)
{
    atomic_store(&s_code, code);
    if (atomic_exchange(&s_on, on_air) == on_air) {
        return;
    }
    app_camera_hw_set_webcam(on_air);
    /* Streaming needs prompt Wi-Fi: hold the station out of deep power save. */
    app_net_set_low_latency(on_air);
    if (on_air) {
        start();
    } else if (s_server) {
        /* The flag is already off, so a streaming handler leaves its loop
         * within POLL_MS and httpd_stop can join the server task. */
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "off air");
    }
}
