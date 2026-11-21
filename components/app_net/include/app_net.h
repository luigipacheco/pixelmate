// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Wi-Fi (credentials from Vibe Mode shared values), network time, mDNS and a
 * small stats web server. Device-only; contracts in ../README.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_NET_STATUS_OFF = 0,     /**< Not started or failed to start. */
    APP_NET_STATUS_NO_CONFIG,   /**< No wifi_ssid in Vibe Mode shared values. */
    APP_NET_STATUS_CONNECTING,
    APP_NET_STATUS_ONLINE,
} app_net_status_t;

/** Start Wi-Fi, SNTP, mDNS `<hostname>.local` and the web server. Call once
 * from task context. Returns ESP_ERR_NOT_FOUND when no Wi-Fi is configured. */
esp_err_t app_net_start(const char *hostname);

/** Current status; writes the IPv4 address (network byte order) when online. */
app_net_status_t app_net_status(uint32_t *ipv4);

/** Replace the JSON served at /api/stats (copied; at most APP_NET_JSON_MAX-1 bytes). */
enum { APP_NET_JSON_MAX = 1024 };
esp_err_t app_net_publish(const char *json);

/** Take a timezone sent by the web page: minutes west of UTC, as returned by
 * JavaScript getTimezoneOffset(). Returns true once per new value. */
bool app_net_take_timezone(int *minutes_west);

#ifdef __cplusplus
}
#endif
