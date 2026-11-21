// SPDX-License-Identifier: Apache-2.0
/* Captive DNS for the setup hotspot: every A query is answered with the
 * hotspot's own address, so a phone's "is there internet?" probe lands on our
 * web server and the phone offers the sign-in (setup) page. Other query types
 * get an empty answer. Runs only while the hotspot is up.
 *
 * Lifecycle: start/stop come from the event loop task only. The server task
 * owns its socket and buffer and frees them on exit; stop waits for that. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "app_net_priv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "app_net_dns";

enum {
    DNS_PORT = 53,
    DNS_BUF = 512,              /* Classic DNS over UDP limit. */
    DNS_HEADER = 12,
    DNS_ANSWER = 16,            /* Name pointer, type, class, TTL, length, IPv4. */
    DNS_TTL_S = 30,             /* Short: real answers return soon after setup. */
    DNS_STACK = 3072,
    DNS_PRIORITY = 4,
    RECV_TIMEOUT_MS = 500,      /* How often the task checks for stop. */
    STOP_WAIT_MS = 2000,
    TYPE_A = 1,
    CLASS_IN = 1,
};

static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static volatile bool s_run;
static uint32_t s_ipv4;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Turn the query in buf[0..len) into a reply in place; returns its length or 0. */
static int build_reply(uint8_t *buf, int len)
{
    if (len < DNS_HEADER || (buf[2] & 0x80) || (buf[2] & 0x78) || get16(buf + 4) < 1) {
        return 0;   /* Not a standard query. */
    }
    int pos = DNS_HEADER;
    while (pos < len && buf[pos]) {
        if (buf[pos] & 0xC0) {
            return 0;   /* No compression in questions. */
        }
        pos += buf[pos] + 1;
    }
    if (pos + 5 > len) {
        return 0;
    }
    const uint16_t qtype = get16(buf + pos + 1), qclass = get16(buf + pos + 3);
    pos += 5;   /* End of the first question; extra questions/records are dropped. */
    const bool answer = qtype == TYPE_A && (qclass & 0x7FFF) == CLASS_IN;
    if (answer && pos + DNS_ANSWER > DNS_BUF) {
        return 0;
    }
    buf[2] = 0x84 | (buf[2] & 0x01);   /* Response, authoritative, keep RD. */
    buf[3] = 0x00;                     /* No error. */
    put16(buf + 4, 1);
    put16(buf + 6, answer ? 1 : 0);
    put16(buf + 8, 0);
    put16(buf + 10, 0);
    if (!answer) {
        return pos;
    }
    uint8_t *a = buf + pos;
    put16(a, 0xC000 | DNS_HEADER);     /* Pointer to the question's name. */
    put16(a + 2, TYPE_A);
    put16(a + 4, CLASS_IN);
    put16(a + 6, 0);
    put16(a + 8, DNS_TTL_S);
    put16(a + 10, 4);
    memcpy(a + 12, &s_ipv4, 4);        /* Already network order. */
    return pos + DNS_ANSWER;
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t *buf = malloc(DNS_BUF);
    const int sock = buf ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : -1;
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(DNS_PORT), .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGW(TAG, "cannot serve DNS (errno %d)", errno);
    } else {
        const struct timeval timeout = {.tv_sec = 0, .tv_usec = RECV_TIMEOUT_MS * 1000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        while (s_run) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            const int len = recvfrom(sock, buf, DNS_BUF, 0, (struct sockaddr *)&from, &from_len);
            const int reply = len > 0 ? build_reply(buf, len) : 0;
            if (reply > 0) {
                sendto(sock, buf, reply, 0, (struct sockaddr *)&from, from_len);
            }
        }
    }
    if (sock >= 0) {
        close(sock);
    }
    free(buf);
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t app_net_dns_start(uint32_t ipv4)
{
    if (s_task) {
        return ESP_OK;
    }
    if (!s_done && !(s_done = xSemaphoreCreateBinary())) {
        return ESP_ERR_NO_MEM;
    }
    s_ipv4 = ipv4;
    s_run = true;
    if (xTaskCreate(dns_task, "captive_dns", DNS_STACK, NULL, DNS_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        s_run = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_net_dns_stop(void)
{
    if (!s_task) {
        return;
    }
    s_run = false;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(STOP_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "DNS task did not stop in time");
    }
    s_task = NULL;
}
