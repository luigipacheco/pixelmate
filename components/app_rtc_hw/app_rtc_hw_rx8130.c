// SPDX-License-Identifier: Apache-2.0
/* RX8130 real-time clock (Epson; the M5Stack StopWatch's, I2C 0x32 on the
 * board's internal bus, found by port number PIXELMATE_I2C_PORT). Holds UTC.
 * Registers as M5Unified's RX8130_Class uses them (MIT, M5Stack): 0x10..0x16
 * seconds, minutes, hours, weekday (one bit per day), day, month, year (BCD,
 * 2000-2099); 0x1D flags with VLF (bit 1) set after the backup supply failed;
 * 0x1F control 1 with INIEN (bit 4). */
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include "app_rtc_hw.h"
#include "driver/i2c_master.h"

#ifndef PIXELMATE_I2C_PORT
#define PIXELMATE_I2C_PORT 0
#endif
#define RTC_ADDR 0x32
#define RTC_SPEED_HZ 100000
#define RTC_TIMEOUT_MS 50
#define RTC_REG_TIME 0x10
#define RTC_REG_FLAG 0x1D
#define RTC_REG_CTRL1 0x1F
#define RTC_FLAG_VLF 0x02
#define MIN_YEAR 2024            /* Earlier dates are a never-set RTC. */

static i2c_master_dev_handle_t s_rtc;

static esp_err_t chip(void)
{
    if (s_rtc) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_master_get_bus_handle(PIXELMATE_I2C_PORT, &bus);
    if (err == ESP_OK) {
        err = i2c_master_probe(bus, RTC_ADDR, RTC_TIMEOUT_MS);
    }
    if (err == ESP_OK) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR, .scl_speed_hz = RTC_SPEED_HZ,
        };
        err = i2c_master_bus_add_device(bus, &config, &s_rtc);
    }
    return err;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_rtc, &reg, 1, data, len, RTC_TIMEOUT_MS);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

esp_err_t app_rtc_hw_load(void)
{
    esp_err_t err = chip();
    uint8_t flag = 0, t[7] = {0};
    if (err == ESP_OK) {
        err = read_reg(RTC_REG_FLAG, &flag, 1);
    }
    if (err == ESP_OK) {
        err = read_reg(RTC_REG_TIME, t, sizeof(t));
    }
    if (err != ESP_OK) {
        return err;
    }
    const int second = from_bcd(t[0] & 0x7F), minute = from_bcd(t[1] & 0x7F), hour = from_bcd(t[2] & 0x3F);
    const int day = from_bcd(t[4] & 0x3F), month = from_bcd(t[5] & 0x1F), year = 2000 + from_bcd(t[6]);
    if ((flag & RTC_FLAG_VLF) || year < MIN_YEAR || month < 1 || month > 12 || day < 1 || day > 31
            || hour > 23 || minute > 59 || second > 59) {
        return ESP_ERR_INVALID_STATE;
    }
    const struct timeval now = {
        .tv_sec = (time_t)(days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second),
    };
    return settimeofday(&now, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t app_rtc_hw_store(void)
{
    esp_err_t err = chip();
    if (err != ESP_OK) {
        return err;
    }
    const time_t now = time(NULL);
    struct tm utc;
    if (!gmtime_r(&now, &utc) || utc.tm_year + 1900 < MIN_YEAR || utc.tm_year + 1900 > 2099) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Control 1: INIEN on, as M5Unified sets it (keep the other bits). */
    uint8_t ctrl1 = 0;
    if (read_reg(RTC_REG_CTRL1, &ctrl1, 1) == ESP_OK) {
        const uint8_t data[2] = {RTC_REG_CTRL1, (uint8_t)((ctrl1 & ~0x20U) | 0x10U)};
        (void)i2c_master_transmit(s_rtc, data, sizeof(data), RTC_TIMEOUT_MS);
    }
    const uint8_t data[8] = {
        RTC_REG_TIME, to_bcd(utc.tm_sec), to_bcd(utc.tm_min), to_bcd(utc.tm_hour),
        (uint8_t)(1U << (utc.tm_wday & 7)), to_bcd(utc.tm_mday), to_bcd(utc.tm_mon + 1),
        to_bcd((utc.tm_year + 1900) % 100),
    };
    err = i2c_master_transmit(s_rtc, data, sizeof(data), RTC_TIMEOUT_MS);
    if (err == ESP_OK) {
        /* Clear VLF (write 0 to clear; the other flags written 1 stay). */
        const uint8_t clear[2] = {RTC_REG_FLAG, (uint8_t)~RTC_FLAG_VLF};
        err = i2c_master_transmit(s_rtc, clear, sizeof(clear), RTC_TIMEOUT_MS);
    }
    return err;
}
