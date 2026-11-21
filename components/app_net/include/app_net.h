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

/** Take a timezone sent by the web page: minutes west of UTC, as returned by
 * JavaScript getTimezoneOffset(). Returns true once per new value. */
bool app_net_take_timezone(int *minutes_west);

#ifdef __cplusplus
}
#endif
