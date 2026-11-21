// SPDX-License-Identifier: Apache-2.0
/* Wi-Fi station, SNTP and mDNS. The web server lives in app_net_web.c.
 * Status is written by the default event loop task and read by the game loop;
 * a spinlock-protected pair keeps the two consistent. */
#include <string.h>
#include "app_net.h"
#include "app_net_priv.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "mdns.h"
#include "mosaico_shared.h"

static const char *TAG = "app_net";

#define NTP_SERVER "pool.ntp.org"
#define RETRY_DELAY_US (5 * 1000 * 1000)
enum { SSID_MAX = 33, PASS_MAX = 65 };

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_net_status_t s_status;
static uint32_t s_ipv4;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry;
static bool s_started;

static void set_status(app_net_status_t status, uint32_t ipv4)
{
    portENTER_CRITICAL(&s_lock);
    s_status = status;
    s_ipv4 = ipv4;
    portEXIT_CRITICAL(&s_lock);
}

app_net_status_t app_net_status(uint32_t *ipv4)
{
    portENTER_CRITICAL(&s_lock);
    const app_net_status_t status = s_status;
    const uint32_t ip = s_ipv4;
    portEXIT_CRITICAL(&s_lock);
    if (ipv4) {
        *ipv4 = status == APP_NET_STATUS_ONLINE ? ip : 0;
    }
    return status;
}

static void retry_connect(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        set_status(APP_NET_STATUS_CONNECTING, 0);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        set_status(APP_NET_STATUS_CONNECTING, 0);
        /* Back off instead of hammering the access point. */
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, RETRY_DELAY_US);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = data;
        set_status(APP_NET_STATUS_ONLINE, got->ip_info.ip.addr);
        ESP_LOGI(TAG, "online at " IPSTR, IP2STR(&got->ip_info.ip));
    }
}

static esp_err_t start_wifi(const char *ssid, const char *pass, const char *hostname)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_netif, ESP_FAIL, TAG, "wifi netif");
    const esp_timer_create_args_t retry = {.callback = retry_connect, .name = "wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry, &s_retry), TAG, "retry timer");
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL),
                        TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL),
                        TAG, "ip events");
    wifi_config_t cfg = {0};
    /* Fixed-size fields need no NUL at full length (32-byte SSID, 64-char PSK). */
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "wifi config");
    memset(&cfg, 0, sizeof(cfg));
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);   /* Radio naps between beacons. */
    esp_netif_set_hostname(s_netif, hostname);
    return ESP_OK;
}

esp_err_t app_net_start(const char *hostname)
{
    if (s_started) {
        return ESP_OK;
    }
    char ssid[SSID_MAX] = {0};
    char pass[PASS_MAX] = {0};
    if (mosaico_shared_get("wifi_ssid", ssid, sizeof(ssid)) != ESP_OK || !ssid[0]) {
        set_status(APP_NET_STATUS_NO_CONFIG, 0);
        ESP_LOGW(TAG, "no wifi_ssid in shared values; set Wi-Fi in Vibe Mode");
        return ESP_ERR_NOT_FOUND;
    }
    if (mosaico_shared_get("wifi_password", pass, sizeof(pass)) != ESP_OK) {
        pass[0] = '\0';
    }
    esp_err_t err = start_wifi(ssid, pass, hostname);
    memset(pass, 0, sizeof(pass));
    if (err != ESP_OK) {
        set_status(APP_NET_STATUS_OFF, 0);
        return err;
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
    err = app_net_web_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web server failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "joining \"%s\" as %s.local", ssid, hostname);
    return ESP_OK;
}
