// SPDX-License-Identifier: Apache-2.0
/* Wi-Fi station, setup hotspot, SNTP and mDNS. The web server lives in
 * app_net_web.c, the setup portal in app_net_portal.c, captive DNS in
 * app_net_dns.c.
 *
 * Ownership: every Wi-Fi call that changes state runs on the default event
 * loop task (Wi-Fi/IP events plus this component's own request events), so
 * the station, hotspot and join state need no locks there. What other tasks
 * read (status, address, setup flag, join result) sits behind s_lock. */
#include <stdio.h>
#include <string.h>
#include "app_net.h"
#include "app_net_priv.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "mdns.h"
#include "mosaico_shared.h"
#include "nvs.h"

static const char *TAG = "app_net";

#define NTP_SERVER "pool.ntp.org"
/* Reconnect backoff: 5 s doubling to 5 min, so an absent network costs little. */
#define RETRY_FIRST_US (5LL * 1000 * 1000)
#define RETRY_MAX_US (300LL * 1000 * 1000)
/* An open hotspot must not outlive the person setting it up. */
#define SETUP_TIMEOUT_US (10LL * 60 * 1000 * 1000)
/* After a successful join the hotspot lingers so the phone can read the new address. */
#define SETUP_LINGER_US (20LL * 1000 * 1000)
#define CREDS_NAMESPACE "app_net"   /* Owned by this component; see README. */
#define SETUP_NAME_PREFIX "PIXELMATE-"
#define STATION_PS WIFI_PS_MAX_MODEM   /* Uses LISTEN_INTERVAL; MIN_MODEM wakes every DTIM. */

enum {
    FAIL_AFTER = 4,          /* Failed attempts (~75 s of backoff) before FAILED. */
    JOIN_ATTEMPTS = 3,       /* Tries for a network submitted from the setup page. */
    AP_CHANNEL = 1,
    AP_MAX_CLIENTS = 3,
    SETUP_NAME_MAX = 24,
    POST_WAIT_MS = 100,
    /* Station power save: the radio sleeps through LISTEN_INTERVAL beacons
     * (~0.3 s) instead of waking for every one. Pages answer a little slower. */
    LISTEN_INTERVAL = 3,
};

ESP_EVENT_DEFINE_BASE(APP_NET_EVENT);
enum { REQ_SETUP_START, REQ_SETUP_STOP, REQ_JOIN, REQ_POWER };

typedef struct {
    char ssid[APP_NET_SSID_MAX];
    char pass[APP_NET_PASS_MAX];
} creds_t;

/* Shared with other tasks; guarded by s_lock. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_net_status_t s_station = APP_NET_STATUS_OFF;
static uint32_t s_ipv4;
static bool s_setup;                 /* Requested or running. */
static app_net_join_t s_join;
static uint32_t s_join_ip;
static bool s_low_latency;           /* A stream asked for prompt Wi-Fi (no deep power save). */

/* Event loop task only (initialized by app_net_start before handlers exist). */
static esp_netif_t *s_sta, *s_ap;
static esp_timer_handle_t s_retry, s_setup_timer;
static int64_t s_retry_us = RETRY_FIRST_US;
static uint8_t s_fails, s_join_tries;
static bool s_have_creds;            /* Station config holds a saved network. */
static bool s_wifi_running;
static bool s_ap_running;
static creds_t s_pending;            /* Submitted network until saved or rejected. */

/* Game loop only. */
static bool s_started;
static char s_setup_name[SETUP_NAME_MAX];

static void set_station(app_net_status_t status, uint32_t ipv4)
{
    portENTER_CRITICAL(&s_lock);
    s_station = status;
    s_ipv4 = ipv4;
    portEXIT_CRITICAL(&s_lock);
}

static void set_join(app_net_join_t join, uint32_t ipv4)
{
    portENTER_CRITICAL(&s_lock);
    s_join = join;
    s_join_ip = ipv4;
    portEXIT_CRITICAL(&s_lock);
}

static void set_setup(bool on)
{
    portENTER_CRITICAL(&s_lock);
    s_setup = on;
    portEXIT_CRITICAL(&s_lock);
}

app_net_status_t app_net_status(uint32_t *ipv4)
{
    portENTER_CRITICAL(&s_lock);
    app_net_status_t status = s_station;
    const uint32_t ip = s_ipv4;
    const bool setup = s_setup;
    portEXIT_CRITICAL(&s_lock);
    /* Once the new network is joined, ONLINE wins while the hotspot lingers. */
    if (setup && status != APP_NET_STATUS_ONLINE) {
        status = APP_NET_STATUS_SETUP;
    }
    if (ipv4) {
        *ipv4 = status == APP_NET_STATUS_ONLINE ? ip : 0;
    }
    return status;
}

bool app_net_setup_active(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool on = s_setup;
    portEXIT_CRITICAL(&s_lock);
    return on;
}

app_net_join_t app_net_join_state(uint32_t *ipv4)
{
    portENTER_CRITICAL(&s_lock);
    const app_net_join_t join = s_join;
    const uint32_t ip = s_join_ip;
    portEXIT_CRITICAL(&s_lock);
    if (ipv4) {
        *ipv4 = ip;
    }
    return join;
}

const char *app_net_setup_name(void)
{
    return s_setup_name;
}

/* Station power save: deep (STATION_PS) normally, every-beacon while a
 * stream holds app_net_set_low_latency(true). Event loop task. */
static wifi_ps_type_t station_ps(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool low_latency = s_low_latency;
    portEXIT_CRITICAL(&s_lock);
    return low_latency ? WIFI_PS_MIN_MODEM : STATION_PS;
}

/* ---- Saved networks --------------------------------------------------- */

/* The app's own saved network wins over Vibe Mode's shared values, which
 * this app can only read. */
static bool load_creds(creds_t *c, const char **source)
{
    memset(c, 0, sizeof(*c));
    nvs_handle_t h;
    if (nvs_open(CREDS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(c->ssid);
        const bool found = nvs_get_str(h, "ssid", c->ssid, &n) == ESP_OK && c->ssid[0];
        n = sizeof(c->pass);
        if (found && nvs_get_str(h, "pass", c->pass, &n) != ESP_OK) {
            c->pass[0] = '\0';
        }
        nvs_close(h);
        if (found) {
            *source = "setup";
            return true;
        }
        memset(c, 0, sizeof(*c));
    }
    if (mosaico_shared_get("wifi_ssid", c->ssid, sizeof(c->ssid)) == ESP_OK && c->ssid[0]) {
        if (mosaico_shared_get("wifi_password", c->pass, sizeof(c->pass)) != ESP_OK) {
            c->pass[0] = '\0';
        }
        *source = "Vibe Mode";
        return true;
    }
    memset(c, 0, sizeof(*c));
    return false;
}

static esp_err_t save_creds(const creds_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CREDS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", c->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", c->pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t apply_station(const creds_t *c)
{
    wifi_config_t cfg = {0};
    /* Fixed-size fields need no NUL at full length (32-byte SSID, 64-char PSK). */
    memcpy(cfg.sta.ssid, c->ssid, strnlen(c->ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, c->pass, strnlen(c->pass, sizeof(cfg.sta.password)));
    cfg.sta.listen_interval = LISTEN_INTERVAL;
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof(cfg));
    return err;
}

/* ---- Setup hotspot (event loop task) ---------------------------------- */

static void setup_end(void);

static void setup_begin(void)
{
    if (s_ap_running) {
        return;
    }
    esp_timer_stop(s_retry);
    if (!s_ap) {
        s_ap = esp_netif_create_default_wifi_ap();
        if (s_ap) {
            /* DHCP option 114 tells phones where the sign-in page is. Set it
             * before the hotspot first starts the DHCP server (options cannot
             * change while it runs; an explicit dhcps_stop here would keep it
             * from ever starting). Static string: the netif keeps the pointer. */
            static char uri[] = "http://" APP_NET_SETUP_IP_TEXT "/";
            esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
        }
    }
    wifi_config_t ap = {0};
    const size_t len = strlen(s_setup_name);   /* Always shorter than the 32-byte field. */
    memcpy(ap.ap.ssid, s_setup_name, len);
    ap.ap.ssid_len = (uint8_t)len;
    ap.ap.channel = AP_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = AP_MAX_CLIENTS;
    esp_err_t err = s_ap ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK && !s_wifi_running) {
        err = esp_wifi_start();
        s_wifi_running = err == ESP_OK;
    } else if (err == ESP_OK) {
        /* Pause the station: scans need it idle and the hotspot keeps its channel. */
        esp_wifi_disconnect();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setup hotspot failed: %s", esp_err_to_name(err));
        s_ap_running = true;   /* Let setup_end restore station mode. */
        setup_end();
        return;
    }
    s_ap_running = true;
    esp_wifi_set_ps(WIFI_PS_NONE);
    set_station(s_have_creds ? APP_NET_STATUS_CONNECTING : APP_NET_STATUS_NO_CONFIG, 0);
    set_join(APP_NET_JOIN_IDLE, 0);
    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(s_ap, &ip);
    if (app_net_dns_start(ip.ip.addr) != ESP_OK) {
        ESP_LOGW(TAG, "captive DNS unavailable; open http://" APP_NET_SETUP_IP_TEXT " by hand");
    }
    esp_timer_stop(s_setup_timer);
    esp_timer_start_once(s_setup_timer, (uint64_t)SETUP_TIMEOUT_US);
    ESP_LOGI(TAG, "setup hotspot \"%s\" at " APP_NET_SETUP_IP_TEXT, s_setup_name);
}

/* Close the hotspot and go back to the saved network (or radio off). */
static void setup_end(void)
{
    esp_timer_stop(s_setup_timer);
    memset(&s_pending, 0, sizeof(s_pending));
    if (!s_ap_running) {
        set_setup(false);
        return;
    }
    s_ap_running = false;
    app_net_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    set_setup(false);
    set_join(APP_NET_JOIN_IDLE, 0);
    if (app_net_status(NULL) == APP_NET_STATUS_ONLINE) {
        esp_wifi_set_ps(station_ps());
        ESP_LOGI(TAG, "setup hotspot closed");
        return;
    }
    /* Cancelled or timed out: a rejected network may still be configured. */
    creds_t c;
    const char *source = "";
    s_have_creds = load_creds(&c, &source);
    esp_wifi_disconnect();
    if (s_have_creds && apply_station(&c) == ESP_OK) {
        s_fails = 0;
        s_retry_us = RETRY_FIRST_US;
        set_station(APP_NET_STATUS_CONNECTING, 0);
        esp_wifi_set_ps(station_ps());
        esp_wifi_connect();
    } else {
        s_have_creds = false;
        set_station(APP_NET_STATUS_NO_CONFIG, 0);
        esp_wifi_stop();
        s_wifi_running = false;
    }
    memset(&c, 0, sizeof(c));
    ESP_LOGI(TAG, "setup hotspot closed; %s", s_have_creds ? "rejoining saved network" : "no network saved");
}

static void join_begin(const creds_t *c)
{
    if (!s_ap_running) {
        set_join(APP_NET_JOIN_IDLE, 0);
        return;
    }
    s_pending = *c;
    s_join_tries = 0;
    esp_wifi_disconnect();
    esp_err_t err = apply_station(&s_pending);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "join failed to start: %s", esp_err_to_name(err));
        set_join(APP_NET_JOIN_FAILED, 0);
        return;
    }
    ESP_LOGI(TAG, "setup: trying \"%s\"", s_pending.ssid);
}

static void join_disconnected(uint8_t reason)
{
    if (app_net_join_state(NULL) != APP_NET_JOIN_TRYING || reason == WIFI_REASON_ASSOC_LEAVE) {
        return;   /* Our own disconnects, or the station is paused. */
    }
    if (++s_join_tries < JOIN_ATTEMPTS) {
        esp_wifi_connect();
        return;
    }
    app_net_join_t result = APP_NET_JOIN_FAILED;
    if (reason == WIFI_REASON_NO_AP_FOUND) {
        result = APP_NET_JOIN_NOT_FOUND;
    } else if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
               || reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
        result = APP_NET_JOIN_BAD_PASSWORD;
    }
    ESP_LOGW(TAG, "setup: \"%s\" rejected (reason %u)", s_pending.ssid, reason);
    memset(&s_pending, 0, sizeof(s_pending));
    set_join(result, 0);
}

static void join_succeeded(uint32_t ipv4)
{
    const esp_err_t err = save_creds(&s_pending);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "network joined but not saved: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "setup: joined and saved \"%s\"", s_pending.ssid);
    }
    memset(&s_pending, 0, sizeof(s_pending));
    s_have_creds = true;
    set_join(APP_NET_JOIN_OK, ipv4);
    esp_timer_stop(s_setup_timer);
    esp_timer_start_once(s_setup_timer, (uint64_t)SETUP_LINGER_US);
}

/* ---- Events ------------------------------------------------------------ */

static void retry_connect(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

/* esp_timer task: hand the stop to the event loop that owns the state. */
static void setup_timeout(void *arg)
{
    (void)arg;
    esp_event_post(APP_NET_EVENT, REQ_SETUP_STOP, NULL, 0, pdMS_TO_TICKS(POST_WAIT_MS));
}

static void on_station_drop(const wifi_event_sta_disconnected_t *d)
{
    if (s_ap_running) {
        set_station(s_have_creds ? APP_NET_STATUS_CONNECTING : APP_NET_STATUS_NO_CONFIG, 0);
        join_disconnected(d ? d->reason : 0);
        return;
    }
    if (s_fails < UINT8_MAX) {
        ++s_fails;
    }
    set_station(s_fails >= FAIL_AFTER ? APP_NET_STATUS_FAILED : APP_NET_STATUS_CONNECTING, 0);
    /* Back off instead of hammering the access point. */
    esp_timer_stop(s_retry);
    esp_timer_start_once(s_retry, (uint64_t)s_retry_us);
    s_retry_us = s_retry_us * 2 > RETRY_MAX_US ? RETRY_MAX_US : s_retry_us * 2;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_creds && !s_ap_running) {
            set_station(APP_NET_STATUS_CONNECTING, 0);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        on_station_drop(data);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;
        s_retry_us = RETRY_FIRST_US;
        s_fails = 0;
        set_station(APP_NET_STATUS_ONLINE, got->ip_info.ip.addr);
        ESP_LOGI(TAG, "online at " IPSTR, IP2STR(&got->ip_info.ip));
        if (s_ap_running && app_net_join_state(NULL) == APP_NET_JOIN_TRYING) {
            join_succeeded(got->ip_info.ip.addr);
        }
    } else if (base == APP_NET_EVENT && id == REQ_SETUP_START) {
        setup_begin();
    } else if (base == APP_NET_EVENT && id == REQ_SETUP_STOP) {
        setup_end();
    } else if (base == APP_NET_EVENT && id == REQ_POWER) {
        if (s_wifi_running && !s_ap_running) {   /* The hotspot keeps PS_NONE. */
            esp_wifi_set_ps(station_ps());
        }
    } else if (base == APP_NET_EVENT && id == REQ_JOIN && data) {
        join_begin(data);
        memset(data, 0, sizeof(creds_t));   /* The loop's copy of the password. */
    }
}

/* ---- Requests from other tasks ---------------------------------------- */

esp_err_t app_net_setup_start(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    set_setup(true);   /* Status shows SETUP at once; the loop does the work. */
    const esp_err_t err = esp_event_post(APP_NET_EVENT, REQ_SETUP_START, NULL, 0,
                                         pdMS_TO_TICKS(POST_WAIT_MS));
    if (err != ESP_OK) {
        set_setup(false);
    }
    return err;
}

esp_err_t app_net_set_low_latency(bool on)
{
    portENTER_CRITICAL(&s_lock);
    const bool changed = s_low_latency != on;
    s_low_latency = on;
    portEXIT_CRITICAL(&s_lock);
    if (!changed || !s_started) {
        return ESP_OK;   /* Applied when the station starts. */
    }
    return esp_event_post(APP_NET_EVENT, REQ_POWER, NULL, 0, pdMS_TO_TICKS(POST_WAIT_MS));
}

esp_err_t app_net_setup_stop(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post(APP_NET_EVENT, REQ_SETUP_STOP, NULL, 0, pdMS_TO_TICKS(POST_WAIT_MS));
}

esp_err_t app_net_join(const char *ssid, const char *pass)
{
    if (!ssid || !pass || !app_net_setup_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    creds_t c = {0};
    strlcpy(c.ssid, ssid, sizeof(c.ssid));
    strlcpy(c.pass, pass, sizeof(c.pass));
    set_join(APP_NET_JOIN_TRYING, 0);   /* So a status poll right after never sees the old result. */
    const esp_err_t err = esp_event_post(APP_NET_EVENT, REQ_JOIN, &c, sizeof(c), pdMS_TO_TICKS(POST_WAIT_MS));
    memset(&c, 0, sizeof(c));
    if (err != ESP_OK) {
        set_join(APP_NET_JOIN_FAILED, 0);
    }
    return err;
}

/* ---- Start ------------------------------------------------------------- */

static esp_err_t init_wifi(const char *hostname)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_sta = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta, ESP_FAIL, TAG, "wifi netif");
    const esp_timer_create_args_t retry = {.callback = retry_connect, .name = "wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry, &s_retry), TAG, "retry timer");
    const esp_timer_create_args_t setup = {.callback = setup_timeout, .name = "wifi_setup"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&setup, &s_setup_timer), TAG, "setup timer");
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    /* Keep our configs out of the driver's own NVS records, which Vibe Mode may use. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL),
                        TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL),
                        TAG, "ip events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_NET_EVENT, ESP_EVENT_ANY_ID, on_event, NULL),
                        TAG, "app events");
    esp_netif_set_hostname(s_sta, hostname);
    return ESP_OK;
}

static esp_err_t start_station(const creds_t *c)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(apply_station(c), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    s_wifi_running = true;
    esp_wifi_set_ps(station_ps());   /* Radio naps between beacons. */
    return ESP_OK;
}

esp_err_t app_net_start(const char *hostname)
{
    if (s_started) {
        return ESP_OK;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_setup_name, sizeof(s_setup_name), SETUP_NAME_PREFIX "%02X%02X", mac[4], mac[5]);

    creds_t c;
    const char *source = "";
    s_have_creds = load_creds(&c, &source);
    esp_err_t err = init_wifi(hostname);
    if (err == ESP_OK && s_have_creds) {
        set_station(APP_NET_STATUS_CONNECTING, 0);
        err = start_station(&c);
    }
    if (err == ESP_OK && s_have_creds) {
        ESP_LOGI(TAG, "joining \"%s\" (saved by %s) as %s.local", c.ssid, source, hostname);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "no network saved; setup hotspot will be %s", s_setup_name);
    }
    memset(&c, 0, sizeof(c));
    if (err != ESP_OK) {
        set_station(APP_NET_STATUS_OFF, 0);
        return err;
    }
    if (!s_have_creds) {
        set_station(APP_NET_STATUS_NO_CONFIG, 0);
    }
    s_started = true;

    /* SNTP starts by itself once the station has an address. */
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    if (esp_netif_sntp_init(&sntp) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed; the clock stays unset");
    }
    err = mdns_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        mdns_hostname_set(hostname);
        mdns_service_add(NULL, "_http", "_tcp", APP_NET_HTTP_PORT, NULL, 0);
    }
    /* One server on port 80 for both the home network and the setup hotspot. */
    err = app_net_web_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web server failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}
