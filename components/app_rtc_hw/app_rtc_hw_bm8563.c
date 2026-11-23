// SPDX-License-Identifier: Apache-2.0
/* BM8563 real-time clock of the M5Stack CoreS3 (I2C 0x51 on the internal
 * bus). It keeps counting from the battery while the board is off, so the pet
 * knows the time at boot without Wi-Fi. Holds UTC; the timezone is applied by
 * the app as usual. */
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include "app_rtc_hw.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"

#define I2C_BUS "i2c_master"
#define RTC_ADDR 0x51
#define RTC_SPEED_HZ 400000
#define RTC_TIMEOUT_MS 50
#define RTC_REG_SECONDS 0x02     /* 7 registers: s, min, h, day, weekday, month, year. */
#define RTC_VL_BIT 0x80          /* Seconds register: power was lost, time is invalid. */
#define RTC_CENTURY_BIT 0x80     /* Month register: set = 19xx; this code only writes 20xx. */
#define MIN_YEAR 2024            /* Earlier dates are a never-set RTC. */

static i2c_master_dev_handle_t s_rtc;   /* On the board's bus for the app's lifetime. */

static esp_err_t chip(void)
{
    if (s_rtc) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = esp_board_periph_ref_handle(I2C_BUS, (void **)&bus);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_probe(bus, RTC_ADDR, RTC_TIMEOUT_MS);
    if (err == ESP_OK) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = RTC_ADDR,
            .scl_speed_hz = RTC_SPEED_HZ,
        };
        err = i2c_master_bus_add_device(bus, &config, &s_rtc);
    }
    if (err != ESP_OK) {
        esp_board_periph_unref_handle(I2C_BUS);
        s_rtc = NULL;
    }
    return err;
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
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t reg = RTC_REG_SECONDS;
    uint8_t t[7] = {0};
    err = i2c_master_transmit_receive(s_rtc, &reg, 1, t, sizeof(t), RTC_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    const int second = from_bcd(t[0] & 0x7F), minute = from_bcd(t[1] & 0x7F), hour = from_bcd(t[2] & 0x3F);
    const int day = from_bcd(t[3] & 0x3F), month = from_bcd(t[5] & 0x1F);
    const int year = ((t[5] & RTC_CENTURY_BIT) ? 1900 : 2000) + from_bcd(t[6]);
    if ((t[0] & RTC_VL_BIT) || year < MIN_YEAR || month < 1 || month > 12 || day < 1 || day > 31
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
    /* Writing the seconds register also clears the power-lost flag. */
    const uint8_t data[8] = {
        RTC_REG_SECONDS, to_bcd(utc.tm_sec), to_bcd(utc.tm_min), to_bcd(utc.tm_hour), to_bcd(utc.tm_mday),
        (uint8_t)utc.tm_wday, to_bcd(utc.tm_mon + 1), to_bcd((utc.tm_year + 1900) % 100),
    };
    return i2c_master_transmit(s_rtc, data, sizeof(data), RTC_TIMEOUT_MS);
}
