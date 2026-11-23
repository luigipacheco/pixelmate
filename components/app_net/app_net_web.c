// SPDX-License-Identifier: Apache-2.0
/* Stats web server: GET / (page), GET /api/stats (JSON), POST /api/tz, plus
 * the setup portal routes (app_net_portal.c) and the photo booth gallery
 * (app_net_photos.c). One server on port 80 answers on both the home network
 * and the setup hotspot. Handlers run on the httpd task. The JSON is a snapshot published by the game
 * loop under a mutex; the timezone travels back through a spinlocked slot. */
#include <stdlib.h>
#include <string.h>
#include "app_net.h"
#include "app_net_priv.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_net_web";

enum {
    TZ_MIN = -14 * 60,    /* JavaScript offsets span UTC+14 .. UTC-12. */
    TZ_MAX = 12 * 60,
    QUERY_MAX = 48,
    SLEEP_JSON_MAX = 640,
    HTTPD_STACK = 6144,
    HTTPD_SOCKETS = 5,    /* Leaves lwIP sockets for captive DNS and ESP-Iris TCP. */
    HTTPD_ROUTES = 20,    /* Stats, time zone and gallery routes, with room to grow. */
};

static SemaphoreHandle_t s_json_lock;
static char s_json[APP_NET_JSON_MAX] = "{}";
static portMUX_TYPE s_tz_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_tz_minutes;
static bool s_tz_pending;
static httpd_handle_t s_server;
static char s_sleep_json[SLEEP_JSON_MAX] = "{\"valid\":0}";

static const char PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>Bitsy - PixelMate</title><style>"
    ":root{--c:#d9ff00;--a:#ff4c01;--d:#2c3300}"
    "body{margin:0;background:#000;color:var(--c);font:16px/1.4 ui-monospace,Menlo,Consolas,monospace;"
    "display:flex;justify-content:center}"
    "main{width:100%;max-width:420px;padding:20px 16px;box-sizing:border-box}"
    "h1{font-size:14px;letter-spacing:.25em;margin:0 0 18px;color:var(--a)}"
    ".big{font-size:64px;line-height:1}.sub{opacity:.6;font-size:13px}"
    ".bar{height:14px;background:var(--d);margin:6px 0 14px}.bar i{display:block;height:100%;background:var(--c)}"
    ".row{display:flex;justify-content:space-between;margin:4px 0}"
    ".hearts{font-size:24px;color:var(--a);letter-spacing:4px}"
    ".chart{display:flex;align-items:flex-end;gap:6px;height:110px;margin:10px 0 4px}"
    ".chart div{flex:1;background:var(--c);min-height:2px}.chart div.miss{background:var(--d)}"
    ".days{display:flex;gap:6px;font-size:11px;opacity:.6}.days span{flex:1;text-align:center}"
    "section{border-top:1px dashed var(--d);padding-top:14px;margin-top:16px}"
    "</style></head><body><main><h1>BITSY</h1><p>PIXELMATE</p>"
    "<div id=walk><div class=sub>TODAY</div><div class=big id=today>-</div><div class=sub id=goal></div>"
    "<div class=bar><i id=gbar></i></div></div>"
    "<div class=row><span>ENERGY</span><span id=food></span></div><div class=bar><i id=fbar></i></div>"
    "<div class=hearts id=hp></div><div class=sub id=state></div>"
    "<section id=week><div class=row><span>LAST 7 DAYS</span><span id=streak></span></div>"
    "<div class=chart id=chart></div><div class=days id=days></div></section>"
    "<section><div class=row><span id=totl>TOTAL</span><span id=total></span></div>"
    "<div class=row><span>SPARKS</span><span id=sparks></span></div>"
    "<div class=row><span>BATTERY</span><span id=batt></span></div>"
    "<div class=row><span>MOOD</span><span id=mood></span></div></section>"
    "<section><a href=/photos style='color:var(--c)'>PHOTO BOOTH GALLERY</a></section><footer style='opacity:.6;font-size:12px;margin-top:24px'>PixelMate v1.0.0 &middot; made by Luis Pacheco &middot; <a href='https://www.luigipacheco.com' style='color:var(--c)'>www.luigipacheco.com</a></footer></main><script>"
    "const $=i=>document.getElementById(i);"
    "fetch('/api/tz?offset='+new Date().getTimezoneOffset(),{method:'POST'}).catch(()=>{});"
    "const pct=(a,b)=>Math.min(100,b?Math.round(a*100/b):0)+'%';"
    "async function tick(){try{const s=await(await fetch('/api/stats')).json();"
    "$('today').textContent=s.steps_today;$('goal').textContent='GOAL '+s.goal;"
    "$('gbar').style.width=pct(s.steps_today,s.goal);"
    "const e=s.fullness;$('food').textContent=e<15?'STARVING':e<40?'HUNGRY':e<75?'PECKISH':'FULL';"
    "$('fbar').style.width=e+'%';"
    "$('hp').textContent='\\u2665'.repeat(s.hp)+'\\u2661'.repeat(s.hp_max-s.hp);"
    "$('state').textContent=s.fainted?'FAINTED - WALK TO REVIVE':(s.clock?'':'CLOCK NOT SET YET');"
    "$('streak').textContent=s.streak+' DAY STREAK';"
    "const h=s.history.slice().reverse(),m=Math.max(s.goal,...h,1);"
    "$('chart').innerHTML=h.map(v=>'<div class=\"'+(v>=s.food_need?'':'miss')+'\" style=\"height:'"
    "+Math.round(v*100/m)+'%\"></div>').join('');"
    "const n=['SU','MO','TU','WE','TH','FR','SA'],d=new Date().getDay();"
    "$('days').innerHTML=h.map((_,i)=>'<span>'+n[(d+i)%7]+'</span>').join('');"
    "$('total').textContent=s.desk?s.pats:s.steps_total;$('sparks').textContent=s.sparks;"
    "if(s.desk){$('walk').hidden=$('week').hidden=true;$('totl').textContent='PATS'}"
    "$('batt').textContent=s.battery<0?'-':s.battery+'%'+(s.charging?' charging':'');"
    "$('mood').textContent=s.mood.toUpperCase()"
    "}catch(e){$('state').textContent='PET OFFLINE'}}"
    "tick();setInterval(tick,5000);"
    "</script></body></html>";

esp_err_t app_net_publish(const char *json)
{
    if (!json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_json_lock) {
        s_json_lock = xSemaphoreCreateMutex();   /* Game loop only: no creation race. */
        if (!s_json_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (strlen(json) >= sizeof(s_json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    xSemaphoreTake(s_json_lock, portMAX_DELAY);
    strcpy(s_json, json);
    xSemaphoreGive(s_json_lock);
    return ESP_OK;
}

bool app_net_take_timezone(int *minutes_west)
{
    bool pending;
    portENTER_CRITICAL(&s_tz_lock);
    pending = s_tz_pending;
    if (pending && minutes_west) {
        *minutes_west = s_tz_minutes;
    }
    s_tz_pending = false;
    portEXIT_CRITICAL(&s_tz_lock);
    return pending;
}

esp_err_t app_net_publish_sleep(const char *json)
{
    if (!json || strlen(json) >= sizeof(s_sleep_json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_json_lock && !(s_json_lock = xSemaphoreCreateMutex())) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_json_lock, portMAX_DELAY);
    strcpy(s_sleep_json, json);
    xSemaphoreGive(s_json_lock);
    return ESP_OK;
}

static esp_err_t get_sleep(httpd_req_t *req)
{
    char *copy = malloc(SLEEP_JSON_MAX);
    if (!copy) {
        return httpd_resp_send_500(req);
    }
    xSemaphoreTake(s_json_lock, portMAX_DELAY);
    memcpy(copy, s_sleep_json, sizeof(s_sleep_json));
    xSemaphoreGive(s_json_lock);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_sendstr(req, copy);
    free(copy);
    return err;
}

static esp_err_t get_page(httpd_req_t *req)
{
    if (app_net_setup_active()) {
        return app_net_portal_page(req);   /* The phone on the hotspot lands here. */
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t get_stats(httpd_req_t *req)
{
    /* Copy out under the lock so a slow client never blocks the game loop. */
    char *copy = malloc(APP_NET_JSON_MAX);
    if (!copy) {
        return httpd_resp_send_500(req);
    }
    if (s_json_lock) {
        xSemaphoreTake(s_json_lock, portMAX_DELAY);
    }
    memcpy(copy, s_json, sizeof(s_json));
    if (s_json_lock) {
        xSemaphoreGive(s_json_lock);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_sendstr(req, copy);
    free(copy);
    return err;
}

static esp_err_t post_tz(httpd_req_t *req)
{
    char query[QUERY_MAX], value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK
            || httpd_query_key_value(query, "offset", value, sizeof(value)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "offset required");
    }
    char *end = NULL;
    const long minutes = strtol(value, &end, 10);
    if (end == value || *end || minutes < TZ_MIN || minutes > TZ_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "offset out of range");
    }
    portENTER_CRITICAL(&s_tz_lock);
    s_tz_minutes = (int)minutes;
    s_tz_pending = true;
    portEXIT_CRITICAL(&s_tz_lock);
    return httpd_resp_sendstr(req, "ok");
}

esp_err_t app_net_web_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    if (!s_json_lock && !(s_json_lock = xSemaphoreCreateMutex())) {
        return ESP_ERR_NO_MEM;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = APP_NET_HTTP_PORT;
    config.stack_size = HTTPD_STACK;
    config.lru_purge_enable = true;
    config.max_open_sockets = HTTPD_SOCKETS;
    config.max_uri_handlers = HTTPD_ROUTES;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_page},
        {.uri = "/api/stats", .method = HTTP_GET, .handler = get_stats},
        {.uri = "/api/tz", .method = HTTP_POST, .handler = post_tz},
        {.uri = "/api/sleep", .method = HTTP_GET, .handler = get_sleep},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "route %s: %s", routes[i].uri, esp_err_to_name(err));
        }
    }
    err = app_net_portal_register(s_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "setup portal: %s", esp_err_to_name(err));
    }
    err = app_net_photos_register(s_server);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "photo gallery: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}
