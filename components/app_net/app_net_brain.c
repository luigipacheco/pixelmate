// SPDX-License-Identifier: Apache-2.0
/* Routes for a "brain" program on the local network (docs/LEARNING.md):
 *   GET  /api/diary?since=N   the pet's event diary from entry N on
 *   GET  /api/behaviors       the acts (behaviours as data) now in use
 *   POST /api/behaviors       a new pack of acts, in the line format
 * The game owns the data; this file only moves bytes through the hooks it
 * registered. Like the stats page, these routes are open to the local network:
 * a pack is range-checked, so the worst a stranger can do is teach the pet a
 * silly face. */
#include <stdlib.h>
#include <string.h>
#include "app_net.h"
#include "app_net_priv.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "app_net_brain";

enum {
    DIARY_CHUNK = 1024,
    REPLY_MAX = 128,
    QUERY_MAX = 32,
};

static app_net_brain_t s_brain;
static bool s_brain_set;

void app_net_set_brain(const app_net_brain_t *brain)
{
    if (brain) {
        s_brain = *brain;
        s_brain_set = true;
    } else {
        s_brain_set = false;
    }
}

static esp_err_t no_brain(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "not ready");
}

static esp_err_t get_diary(httpd_req_t *req)
{
    if (!s_brain_set || !s_brain.diary) {
        return no_brain(req);
    }
    uint32_t since = 0;
    char query[QUERY_MAX], value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK
            && httpd_query_key_value(query, "since", value, sizeof(value)) == ESP_OK) {
        since = (uint32_t)strtoul(value, NULL, 10);
    }
    char *chunk = malloc(DIARY_CHUNK);
    if (!chunk) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr_chunk(req, "{\"events\":[");
    bool first = true;
    uint32_t next = since;
    while (err == ESP_OK) {
        const int n = s_brain.diary(s_brain.ctx, next, chunk, DIARY_CHUNK, &next);
        if (n <= 0) {
            break;
        }
        if (!first) {
            err = httpd_resp_sendstr_chunk(req, ",");
        }
        first = false;
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, chunk, n);
        }
    }
    if (err == ESP_OK) {
        snprintf(chunk, DIARY_CHUNK, "],\"next\":%lu}", (unsigned long)next);
        err = httpd_resp_sendstr_chunk(req, chunk);
    }
    free(chunk);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

static esp_err_t get_behaviors(httpd_req_t *req)
{
    if (!s_brain_set || !s_brain.acts_get) {
        return no_brain(req);
    }
    char *json = malloc(APP_NET_BRAIN_JSON_MAX);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    const int n = s_brain.acts_get(s_brain.ctx, json, APP_NET_BRAIN_JSON_MAX);
    esp_err_t err;
    if (n < 0) {
        err = httpd_resp_send_500(req);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_send(req, json, n);
    }
    free(json);
    return err;
}

static esp_err_t post_behaviors(httpd_req_t *req)
{
    if (!s_brain_set || !s_brain.acts_post) {
        return no_brain(req);
    }
    if (req->content_len == 0 || req->content_len > APP_NET_BRAIN_TEXT_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pack is empty or too large");
    }
    char *body = malloc(req->content_len + 1);
    if (!body) {
        return httpd_resp_send_500(req);
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    body[got] = '\0';
    char reply[REPLY_MAX] = "";
    const bool ok = s_brain.acts_post(s_brain.ctx, body, reply, sizeof(reply));
    free(body);
    ESP_LOGI(TAG, "behaviors: %s", reply);
    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, reply);
}

esp_err_t app_net_brain_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/diary", .method = HTTP_GET, .handler = get_diary},
        {.uri = "/api/behaviors", .method = HTTP_GET, .handler = get_behaviors},
        {.uri = "/api/behaviors", .method = HTTP_POST, .handler = post_behaviors},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
