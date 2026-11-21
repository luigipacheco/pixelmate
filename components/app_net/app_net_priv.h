// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Private to app_net. app_net.c owns all Wi-Fi state on the default event
 * loop task; the web server and the game loop only post requests to it. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

enum {
    APP_NET_HTTP_PORT = 80,
    APP_NET_SSID_MAX = 33,     /* 32 bytes + NUL. */
    APP_NET_PASS_MAX = 65,     /* 63-char passphrase or 64 hex digits + NUL. */
};

/** Setup hotspot address (lwIP's soft-AP default). */
#define APP_NET_SETUP_IP_TEXT "192.168.4.1"

/** Result of the last network submitted from the setup page. */
typedef enum {
    APP_NET_JOIN_IDLE = 0,     /* Nothing submitted in this setup. */
    APP_NET_JOIN_TRYING,
    APP_NET_JOIN_OK,           /* Joined and saved; the hotspot closes soon. */
    APP_NET_JOIN_BAD_PASSWORD,
    APP_NET_JOIN_NOT_FOUND,
    APP_NET_JOIN_FAILED,
} app_net_join_t;

/** True while the setup hotspot runs (any task). */
bool app_net_setup_active(void);

/** Setup page result; *ipv4 (network order) is set once joined. Any task. */
app_net_join_t app_net_join_state(uint32_t *ipv4);

/** Ask the event loop to try (and on success save) a network. Setup only. */
esp_err_t app_net_join(const char *ssid, const char *pass);

/** Start the HTTP server (stats page and APIs, plus the setup portal). */
esp_err_t app_net_web_start(void);

/** Register the setup portal routes and captive redirect (app_net_portal.c). */
esp_err_t app_net_portal_register(httpd_handle_t server);

/** Setup page for GET / while the hotspot runs. */
esp_err_t app_net_portal_page(httpd_req_t *req);

/** Captive DNS: answer every A query with `ipv4` (network order). Called from
 * the event loop task only; stop waits for the server task to exit. */
esp_err_t app_net_dns_start(uint32_t ipv4);
void app_net_dns_stop(void);
