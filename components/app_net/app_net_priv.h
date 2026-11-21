// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Private to app_net. */
#include "esp_err.h"

enum { APP_NET_HTTP_PORT = 80 };

/** Start the HTTP server (page, /api/stats, /api/tz). */
esp_err_t app_net_web_start(void);
