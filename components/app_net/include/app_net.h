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

/* A "brain" program on the local network reads the pet's diary and sends new
 * behaviours (docs/LEARNING.md). The game provides these hooks; they run on
 * the web server's task, so they must lock what they share with the game. */
enum {
    APP_NET_BRAIN_TEXT_MAX = 4096,    /**< Largest POST /api/behaviors body. */
    APP_NET_BRAIN_JSON_MAX = 6144,    /**< Buffer for GET /api/behaviors. */
};
typedef struct {
    void *ctx;
    /** Diary entries from number `since` on, as comma-separated JSON objects.
     *  Returns the bytes written (0 = no more) and the number to ask for next. */
    int (*diary)(void *ctx, uint32_t since, char *out, size_t cap, uint32_t *next);
    /** The behaviours in use, as JSON. Returns the length or -1. */
    int (*acts_get)(void *ctx, char *out, size_t cap);
    /** A new pack (text). Returns true when accepted; `reply` says what happened. */
    bool (*acts_post)(void *ctx, const char *body, char *reply, size_t cap);
} app_net_brain_t;

/** Set (or with NULL, remove) the hooks behind /api/diary and /api/behaviors. */
void app_net_set_brain(const app_net_brain_t *brain);

/** Replace the JSON served at GET /api/sleep (what the last deep sleep did). */
esp_err_t app_net_publish_sleep(const char *json);

#ifdef __cplusplus
}
#endif
