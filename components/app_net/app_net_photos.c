// SPDX-License-Identifier: Apache-2.0
/* Photo booth gallery on the web: GET /photos (page), GET /api/photos (JSON
 * list, newest first) and GET /photo?id=<seq>[&px=<side>] (a photo as PNG, a
 * GIF as GIF, encoded on the fly, upscaled by whole pixels to about px).
 * Handlers run on the httpd task and only read the thread-safe
 * app_photo_store; buffers are per request, in PSRAM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_net_priv.h"
#include "app_photo.h"
#include "app_photo_store.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "app_net_photos";

enum {
    QUERY_MAX = 32,
    JSON_HEAD_MAX = 160,
    JSON_ITEM_MAX = 96,             /* One list item; the longest is about 80. */
    DEFAULT_PX = 256,               /* A sensible size to save. */
    GIF_FRAME_MS = 133,             /* Recorded at 7.5 frames per second. */
    OUT_CAP = 128 * 1024,           /* Real GIFs at 4x are ~20 KB; retry at 1x if not. */
};

static const char PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>PixelMate photos</title><style>"
    ":root{--c:#d9ff00;--a:#ff4c01;--d:#2c3300}"
    "body{margin:0;background:#000;color:var(--c);font:16px/1.4 ui-monospace,Menlo,Consolas,monospace;"
    "display:flex;justify-content:center}"
    "main{width:100%;max-width:420px;padding:20px 16px;box-sizing:border-box}"
    "h1{font-size:14px;letter-spacing:.25em;margin:0 0 18px;color:var(--a)}"
    "a{color:var(--c)}.sub{opacity:.6;font-size:13px}"
    ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:12px}"
    ".grid a{position:relative;display:block}.grid img{width:100%;display:block;image-rendering:pixelated;"
    "background:var(--d)}.grid span{position:absolute;left:4px;bottom:4px;font-size:10px;background:#000;"
    "padding:0 3px}</style></head><body><main><h1>PIXELMATE PHOTOS</h1>"
    "<div class=sub id=count>LOADING...</div><div class=grid id=grid></div>"
    "<p class=sub>TAP A PICTURE TO SAVE IT. TAKE MORE IN THE ARCADE'S PHOTO BOOTH. <a href=/>STATS</a></p>"
    "</main><script>"
    "let last='';"
    "async function load(){try{const g=await(await fetch('/api/photos')).json(),k=JSON.stringify(g);"
    "if(k===last)return;last=k;"
    "document.getElementById('count').textContent=g.max_photos?g.photos+'/'+g.max_photos+' PHOTOS  '"
    "+g.gifs+'/'+g.max_gifs+' GIFS':g.photos+' PHOTOS  '+g.gifs+' GIFS  '+g.free_kb+' KB FREE';"
    "document.getElementById('grid').innerHTML=g.items.map(p=>{const e=p.gif?'gif':'png';"
    "return '<a href=\"/photo?id='+p.id+'\" download=\"pixelmate-'+p.id+'.'+e+'\">'"
    "+'<img loading=lazy src=\"/photo?id='+p.id+'&px=128\" alt=\"'+p.filter+'\"><span>'"
    "+(p.gif?'GIF ':'')+(p.color?'COLOR ':'')+p.res+' '+p.filter+'</span></a>'}).join('')"
    "}catch(e){document.getElementById('count').textContent='PET OFFLINE'}}"
    "load();setInterval(load,10000);"
    "</script></body></html>";

static esp_err_t get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static void *psram(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}

static esp_err_t get_list(httpd_req_t *req)
{
    /* Buffers sized by the store's cap: 14 items on NVS boards, 300 on files. */
    app_photo_store_usage_t use;
    app_photo_store_usage(&use);
    const int max = use.max_items ? use.max_items : 1;
    const int json_max = JSON_HEAD_MAX + max * JSON_ITEM_MAX;
    app_photo_entry_t *entries = psram((size_t)max * sizeof(*entries));
    char *json = psram((size_t)json_max);
    if (!entries || !json) {
        free(entries);
        free(json);
        return httpd_resp_send_500(req);
    }
    const int n = app_photo_store_list(entries, max);
    int len = snprintf(json, (size_t)json_max,
                       "{\"photos\":%u,\"gifs\":%u,\"max_photos\":%u,\"max_gifs\":%u,\"free_kb\":%lu,\"items\":[",
                       use.photos, use.gifs, use.max_photos, use.max_gifs, (unsigned long)(use.free_bytes / 1024));
    for (int i = 0; i < n && len > 0 && len < json_max; ++i) {
        len += snprintf(json + len, (size_t)json_max - (size_t)len,
                        "%s{\"id\":%lu,\"gif\":%s,\"filter\":\"%s\",\"res\":\"%s\",\"color\":%s}",
                        i ? "," : "", (unsigned long)entries[i].seq, entries[i].frames > 1 ? "true" : "false",
                        app_photo_filter_name(entries[i].filter), app_photo_res_name(entries[i].res),
                        entries[i].color ? "true" : "false");
    }
    if (len > 0 && len < json_max) {
        len += snprintf(json + len, (size_t)json_max - (size_t)len, "]}");
    }
    esp_err_t err;
    if (len <= 0 || len >= json_max) {
        err = httpd_resp_send_500(req);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_send(req, json, len);
    }
    free(json);
    free(entries);
    return err;
}

static long query_long(httpd_req_t *req, const char *key, long fallback)
{
    char query[QUERY_MAX], value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK
            || httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return fallback;
    }
    char *end = NULL;
    const long v = strtol(value, &end, 10);
    return end != value && !*end ? v : fallback;
}

/* Encode at the requested scale; a GIF that does not fit falls back to 1x. */
static size_t encode(const app_photo_t *photo, int scale, void *work, uint8_t *out)
{
    if (photo->header.frames == 1) {
        return app_photo_encode_png(photo, scale, out, OUT_CAP);
    }
    const size_t size = app_photo_encode_gif(photo, scale, GIF_FRAME_MS, work, out, OUT_CAP);
    return size || scale == 1 ? size : app_photo_encode_gif(photo, 1, GIF_FRAME_MS, work, out, OUT_CAP);
}

static esp_err_t get_photo(httpd_req_t *req)
{
    const long id = query_long(req, "id", -1);
    const long px = query_long(req, "px", DEFAULT_PX);
    if (id < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id required");
    }
    app_photo_t *photo = psram(sizeof(*photo));
    uint8_t *out = psram(OUT_CAP);
    void *work = psram(APP_PHOTO_GIF_WORK_BYTES);
    esp_err_t err;
    if (!photo || !out || !work) {
        err = httpd_resp_send_500(req);
    } else if (app_photo_store_load_seq((uint32_t)id, photo) != APP_PHOTO_STORE_OK) {
        err = httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such photo");
    } else {
        const long side = app_photo_side(app_photo_lens(&photo->header));
        long scale = px / side;
        scale = scale < 1 ? 1 : (scale * side > APP_PHOTO_MAX_OUT ? APP_PHOTO_MAX_OUT / side : scale);
        const size_t size = encode(photo, (int)scale, work, out);
        if (!size) {
            ESP_LOGW(TAG, "encode %ld failed", id);
            err = httpd_resp_send_500(req);
        } else {
            httpd_resp_set_type(req, photo->header.frames > 1 ? "image/gif" : "image/png");
            /* Ids restart when the whole roll is deleted and the pet reboots. */
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            err = httpd_resp_send(req, (const char *)out, (ssize_t)size);
        }
    }
    free(work);
    free(out);
    free(photo);
    return err;
}

esp_err_t app_net_photos_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/photos", .method = HTTP_GET, .handler = get_page},
        {.uri = "/api/photos", .method = HTTP_GET, .handler = get_list},
        {.uri = "/photo", .method = HTTP_GET, .handler = get_photo},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
