// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Wi-Fi (credentials saved by the app's own setup hotspot, else Vibe Mode
 * shared values), network time, mDNS and a small web server. Device-only;
 * contracts in ../README.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_NET_STATUS_OFF = 0,     /**< Not started or failed to start. */
    APP_NET_STATUS_NO_CONFIG,   /**< No network saved by setup or Vibe Mode. */
    APP_NET_STATUS_CONNECTING,
    APP_NET_STATUS_ONLINE,
    APP_NET_STATUS_FAILED,      /**< Saved network unreachable for a while; still retrying. */
    APP_NET_STATUS_SETUP,       /**< Setup hotspot running (station paused). */
} app_net_status_t;

/** Start Wi-Fi (when a network is saved), SNTP, mDNS `<hostname>.local` and
 * the web server. Call once from task context. With nothing saved the status
 * is NO_CONFIG and setup can be started. */
esp_err_t app_net_start(const char *hostname);

/** Current status; writes the IPv4 address (network byte order) when online. */
app_net_status_t app_net_status(uint32_t *ipv4);

/** Start the setup hotspot (asynchronous). ESP_ERR_INVALID_STATE before
 * app_net_start succeeded. Restarting a running setup is a no-op. */
esp_err_t app_net_setup_start(void);

/** Stop the setup hotspot and resume the saved network (asynchronous). */
esp_err_t app_net_setup_stop(void);

/** Setup hotspot name ("PIXELMATE-XXXX"); "" before app_net_start. */
const char *app_net_setup_name(void);

/** Replace the JSON served at /api/stats (copied; at most APP_NET_JSON_MAX-1 bytes). */
enum { APP_NET_JSON_MAX = 1024 };
esp_err_t app_net_publish(const char *json);

/** Hold Wi-Fi responsive while streaming (e.g. the booth webcam): true keeps
 * the station at WIFI_PS_MIN_MODEM (wakes every DTIM beacon), false returns to
 * the battery-saving deep power save (WIFI_PS_MAX_MODEM, listen interval 3).
 * Any task; asynchronous; safe before app_net_start (applied when it starts). */
esp_err_t app_net_set_low_latency(bool on);

/** Pocket mode: stop the radio (true) before light sleep and start it again
 * (false) on wake; the station then reconnects by itself. Refused while the
 * setup hotspot runs. Call from one task (the game loop). */
esp_err_t app_net_suspend(bool suspend);

/** One access point from a scan (TOOLS: WI-FI). */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool open;
} app_net_ap_t;

/** Start a background scan (the station stays connected). */
esp_err_t app_net_scan_start(void);

/** Results of the last finished scan, strongest first; returns how many were
 * written, or -1 while a scan is still running. */
int app_net_scan_results(app_net_ap_t *out, int max);

/** Take a timezone sent by the web page: minutes west of UTC, as returned by
 * JavaScript getTimezoneOffset(). Returns true once per new value. */
bool app_net_take_timezone(int *minutes_west);

/** Deep-sleep experiment, asked for with
 * POST /api/sleeptest?sec=600&rail=0&touch=0&lp=0 (see app_sleep_hw). */
typedef struct {
    uint32_t seconds;     /**< Timer wake. */
    bool rail_off;        /**< rail=0: cut the switched 3.3 V rail. */
    bool touch_wake;      /**< touch=1: also wake on a touch (rail on only). */
    bool imu_low_power;   /**< lp=1: gyroscope off, accelerometer low power. */
    bool plain;           /**< plain=1: timer wake only; no panel standby, pin holds or rail. */
} app_net_sleep_test_t;

/** Take a pending sleep test. Returns true once per request. */
bool app_net_take_sleep_test(app_net_sleep_test_t *out);

/** Replace the JSON served at GET /api/sleep (the last test's result). */
esp_err_t app_net_publish_sleep(const char *json);

#ifdef __cplusplus
}
#endif
