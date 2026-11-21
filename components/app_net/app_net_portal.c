// SPDX-License-Identifier: Apache-2.0
/* Setup portal served on the setup hotspot: the page (GET / while setup
 * runs), GET /api/scan, POST /api/wifi, GET /api/setup and a captive
 * redirect for every unknown path. Handlers run on the httpd task and refuse
 * to work unless setup is running, so the home network never sees them.
 * The scan is the only Wi-Fi call made outside the event loop task: it is
 * blocking, read-only and the station is paused during setup. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_net_priv.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "app_net_portal";

enum {
    SCAN_MAX = 16,                 /* Networks listed. */
    SCAN_ITEM_MAX = 32 * 6 + 48,   /* Fully escaped SSID plus fields. */
    BODY_MAX = 400,                /* Form body: two URL-encoded fields. */
    FIELD_RAW_MAX = 200,           /* One encoded field (64 chars * 3). */
    PASS_MIN = 8,                  /* WPA2 passphrase minimum; empty = open network. */
};

#define REDIRECT_URL "http://" APP_NET_SETUP_IP_TEXT "/"

static const char PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>PixelMate Wi-Fi</title><style>"
    ":root{--c:#d9ff00;--a:#ff4c01;--d:#2c3300}"
    "body{margin:0;background:#000;color:var(--c);font:16px/1.4 ui-monospace,Menlo,Consolas,monospace;"
    "display:flex;justify-content:center}"
    "main{width:100%;max-width:420px;padding:20px 16px;box-sizing:border-box}"
    "h1{font-size:14px;letter-spacing:.25em;margin:0 0 18px;color:var(--a)}"
    ".sub{opacity:.6;font-size:13px;display:flex;justify-content:space-between;margin:14px 0 6px}"
    "#list button{display:flex;justify-content:space-between;width:100%;margin:0 0 6px;padding:12px;"
    "background:#000;color:var(--c);border:2px solid var(--d);font:inherit;text-align:left}"
    "#list button.on{border-color:var(--c)}#list p{opacity:.6;margin:4px 0}"
    "input{width:100%;box-sizing:border-box;padding:12px;background:#000;color:var(--c);"
    "border:2px solid var(--d);font:inherit;border-radius:0}input:focus{outline:0;border-color:var(--c)}"
    "label{display:flex;gap:8px;align-items:center;font-size:13px;opacity:.6;margin:8px 0 18px}"
    "label input{width:auto}"
    ".go{width:100%;padding:14px;background:var(--c);color:#000;border:0;font:inherit;font-weight:bold;"
    "letter-spacing:.2em}.go:disabled{opacity:.4}"
    ".link{background:none;border:0;color:var(--c);font:inherit;text-decoration:underline;padding:0}"
    "#msg{margin-top:16px;min-height:3em}#msg.err{color:var(--a)}#msg a{color:var(--c)}"
    "</style></head><body><main><h1>PIXELMATE WI-FI</h1>"
    "<div class=sub><span>PICK YOUR NETWORK</span><button class=link id=rescan type=button>RESCAN</button></div>"
    "<div id=list><p>SCANNING...</p></div>"
    "<form id=f><div class=sub>NETWORK</div>"
    "<input id=ssid maxlength=32 autocomplete=off autocapitalize=none spellcheck=false required>"
    "<div class=sub>PASSWORD</div><input id=pass type=password maxlength=64 autocomplete=off>"
    "<label><input type=checkbox id=show>SHOW PASSWORD</label>"
    "<button class=go id=go>SAVE</button></form><div id=msg></div></main><script>"
    "const $=i=>document.getElementById(i);"
    "function msg(t,e){$('msg').textContent=t;$('msg').className=e?'err':''}"
    "const bars=r=>r>-55?'||||':r>-67?'|||':r>-78?'||':'|';"
    "async function scan(){const l=$('list');l.innerHTML='<p>SCANNING...</p>';"
    "try{const r=await(await fetch('/api/scan')).json();l.textContent='';"
    "if(!r.length)l.innerHTML='<p>NO NETWORKS FOUND</p>';"
    "r.forEach(n=>{const b=document.createElement('button'),a=document.createElement('span'),"
    "z=document.createElement('span');b.type='button';a.textContent=n.s;z.textContent=(n.l?'* ':'')+bars(n.r);"
    "b.append(a,z);b.onclick=()=>{$('ssid').value=n.s;[...l.children].forEach(c=>c.classList.remove('on'));"
    "b.classList.add('on');$('pass').focus()};l.append(b)})}"
    "catch(e){l.innerHTML='<p>SCAN FAILED - TAP RESCAN</p>'}}"
    "$('rescan').onclick=scan;$('show').onchange=e=>$('pass').type=e.target.checked?'text':'password';"
    "$('f').onsubmit=async e=>{e.preventDefault();const s=$('ssid').value,p=$('pass').value;"
    "if(p&&p.length<8){msg('PASSWORD NEEDS 8+ CHARACTERS',1);return}"
    "$('go').disabled=true;msg('CONNECTING TO '+s+'...');"
    "try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':"
    "'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:s,pass:p})});if(!r.ok)throw 0;"
    "await poll()}catch(e){msg('COULD NOT SEND - TRY AGAIN',1)}$('go').disabled=false};"
    "const W={password:'WRONG PASSWORD - TRY AGAIN',notfound:'NETWORK NOT FOUND - MOVE CLOSER OR RESCAN',"
    "failed:'COULD NOT CONNECT - TRY AGAIN',idle:'SETUP ENDED ON THE PET'};"
    "async function poll(){for(let i=0;i<40;i++){await new Promise(r=>setTimeout(r,1500));"
    "try{const s=await(await fetch('/api/setup',{cache:'no-store'})).json();"
    "if(s.state=='ok'){done(s.ip);return}if(W[s.state]){msg(W[s.state],1);return}}catch(e){}}"
    "msg('NO ANSWER - LOOK AT THE PET',1)}"
    "function done(ip){const m=$('msg');m.className='';m.textContent='CONNECTED! THIS HOTSPOT CLOSES IN A FEW "
    "SECONDS. REJOIN YOUR HOME WI-FI, THEN OPEN ';"
    "[['http://pixelmate.local','pixelmate.local'],['http://'+ip+'/',ip]].forEach((x,i)=>{const a="
    "document.createElement('a');a.href=x[0];a.textContent=x[1];m.append(i?' OR ':'',a)});"
    "$('f').hidden=true}"
    "scan();"
    "</script></body></html>";

esp_err_t app_net_portal_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t refuse(httpd_req_t *req)
{
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "setup is not running");
}

/* JSON string body for raw SSID bytes (UTF-8 passes through; cap >= n*6+1). */
static void json_escape(char *out, size_t cap, const uint8_t *in, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n && in[i] && o + 7 < cap; ++i) {
        const uint8_t ch = in[i];
        if (ch == '"' || ch == '\\') {
            out[o++] = '\\';
            out[o++] = (char)ch;
        } else if (ch < 0x20) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", ch);
        } else {
            out[o++] = (char)ch;
        }
    }
    out[o] = '\0';
}

static bool seen_before(const wifi_ap_record_t *aps, int index)
{
    for (int i = 0; i < index; ++i) {
        if (!strncmp((const char *)aps[i].ssid, (const char *)aps[index].ssid, sizeof(aps[i].ssid))) {
            return true;
        }
    }
    return false;
}

static esp_err_t get_scan(httpd_req_t *req)
{
    if (!app_net_setup_active()) {
        return refuse(req);
    }
    const wifi_scan_config_t config = {.show_hidden = false};
    uint16_t count = SCAN_MAX;
    wifi_ap_record_t *aps = calloc(SCAN_MAX, sizeof(*aps));
    char *item = malloc(SCAN_ITEM_MAX);
    char *name = malloc(SCAN_ITEM_MAX);
    esp_err_t err = aps && item && name ? esp_wifi_scan_start(&config, true) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&count, aps);   /* Strongest first; frees the list. */
    } else {
        esp_wifi_clear_ap_list();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        count = 0;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send_chunk(req, "[", 1);
    bool first = true;
    for (int i = 0; i < count && err == ESP_OK; ++i) {
        if (!aps[i].ssid[0] || seen_before(aps, i)) {
            continue;
        }
        json_escape(name, SCAN_ITEM_MAX, aps[i].ssid, sizeof(aps[i].ssid));
        const int n = snprintf(item, SCAN_ITEM_MAX, "%s{\"s\":\"%s\",\"r\":%d,\"l\":%d}", first ? "" : ",",
                               name, aps[i].rssi, aps[i].authmode != WIFI_AUTH_OPEN);
        if (n > 0 && n < SCAN_ITEM_MAX) {
            err = httpd_resp_send_chunk(req, item, (ssize_t)n);
            first = false;
        }
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "]", 1);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    free(aps);
    free(item);
    free(name);
    return err;
}

/* application/x-www-form-urlencoded value -> bytes; false if malformed or too long. */
static bool url_decode(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in; ++in) {
        char ch = *in;
        if (ch == '+') {
            ch = ' ';
        } else if (ch == '%') {
            char hex[3] = {in[1], in[1] ? in[2] : '\0', '\0'};
            char *end = NULL;
            const long value = strtol(hex, &end, 16);
            if (!hex[0] || !hex[1] || end != hex + 2 || value == 0) {
                return false;
            }
            ch = (char)value;
            in += 2;
        }
        if (o + 1 >= cap) {
            return false;
        }
        out[o++] = ch;
    }
    out[o] = '\0';
    return true;
}

typedef struct {
    char body[BODY_MAX + 1];
    char raw[FIELD_RAW_MAX];
    char ssid[APP_NET_SSID_MAX];
    char pass[APP_NET_PASS_MAX];
} join_form_t;

static bool read_form(httpd_req_t *req, join_form_t *f)
{
    if (req->content_len == 0 || req->content_len > BODY_MAX) {
        return false;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, f->body + got, req->content_len - got);
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    f->body[got] = '\0';
    if (httpd_query_key_value(f->body, "ssid", f->raw, sizeof(f->raw)) != ESP_OK
            || !url_decode(f->raw, f->ssid, sizeof(f->ssid)) || !f->ssid[0]) {
        return false;
    }
    const esp_err_t pass_err = httpd_query_key_value(f->body, "pass", f->raw, sizeof(f->raw));
    if (pass_err == ESP_ERR_NOT_FOUND) {
        f->raw[0] = '\0';   /* Open network. */
    } else if (pass_err != ESP_OK) {
        return false;       /* Truncated: never join with half a password. */
    }
    if (!url_decode(f->raw, f->pass, sizeof(f->pass))) {
        return false;
    }
    const size_t pass_len = strlen(f->pass);
    return pass_len == 0 || pass_len >= PASS_MIN;
}

static esp_err_t post_wifi(httpd_req_t *req)
{
    if (!app_net_setup_active()) {
        return refuse(req);
    }
    join_form_t *f = calloc(1, sizeof(*f));
    if (!f) {
        return httpd_resp_send_500(req);
    }
    esp_err_t err;
    if (!read_form(req, f)) {
        err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "network name and 8+ character password");
    } else if (app_net_join(f->ssid, f->pass) != ESP_OK) {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy, try again");
    } else {
        err = httpd_resp_sendstr(req, "ok");
    }
    memset(f, 0, sizeof(*f));   /* The password was in here. */
    free(f);
    return err;
}

static esp_err_t get_setup(httpd_req_t *req)
{
    static const char *const states[] = {"idle", "trying", "ok", "password", "notfound", "failed"};
    uint32_t ip = 0;
    app_net_join_t join = app_net_setup_active() ? app_net_join_state(&ip) : APP_NET_JOIN_IDLE;
    if ((size_t)join >= sizeof(states) / sizeof(states[0])) {
        join = APP_NET_JOIN_FAILED;
    }
    char json[64];
    snprintf(json, sizeof(json), "{\"state\":\"%s\",\"ip\":\"%u.%u.%u.%u\"}", states[join],
             (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)(ip >> 24));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

/* Phones probe a known URL to detect captive networks; sending every unknown
 * path to the setup page makes them show it. Outside setup it is a plain 404. */
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    (void)code;
    if (!app_net_setup_active()) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", REDIRECT_URL);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "PixelMate setup: " REDIRECT_URL);
}

esp_err_t app_net_portal_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/scan", .method = HTTP_GET, .handler = get_scan},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = post_wifi},
        {.uri = "/api/setup", .method = HTTP_GET, .handler = get_setup},
    };
    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "route %s: %s", routes[i].uri, esp_err_to_name(err));
            first_err = first_err == ESP_OK ? err : first_err;
        }
    }
    const esp_err_t err = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    return first_err == ESP_OK ? err : first_err;
}
