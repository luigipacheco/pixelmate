// SPDX-License-Identifier: Apache-2.0
/* Battery on M5Stack boards with the M5PM1 power chip (the StopWatch): I2C
 * 0x6E on the internal bus (port 1). It reports the battery and USB input
 * voltages (VBAT 0x22, VIN 0x24, 16-bit little-endian millivolts, as
 * M5Unified's M5PM1_Class reads them); no current or charge, so the percent is
 * an estimate from the cell voltage and "on external power" is USB input
 * present. The board adapter opens the bus; this driver finds it by port. */
#include <stddef.h>
#include "app_power_hw.h"
#include "driver/i2c_master.h"

#define PM1_PORT 1
#define PM1_ADDR 0x6E
#define PM1_SPEED_HZ 100000
#define PM1_TIMEOUT_MS 50
#define PM1_REG_VBAT 0x22
#define PM1_REG_VIN 0x24
#define VIN_PRESENT_MV 4000   /* USB gives ~5 V; floating input reads near 0. */

static i2c_master_dev_handle_t s_pm1;

static esp_err_t chip(void)
{
    if (s_pm1) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_master_get_bus_handle(PM1_PORT, &bus);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PM1_ADDR,
        .scl_speed_hz = PM1_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, &s_pm1);
}

static esp_err_t read_mv(uint8_t reg, uint16_t *mv)
{
    uint8_t data[2] = {0};
    const esp_err_t err = i2c_master_transmit_receive(s_pm1, &reg, 1, data, sizeof(data), PM1_TIMEOUT_MS);
    if (err == ESP_OK) {
        *mv = (uint16_t)(data[0] | (data[1] << 8));
    }
    return err;
}

/* Resting single-cell LiPo voltage to charge, straight lines between points.
 * Reads high while charging: an estimate, not a gauge. */
static uint8_t percent_from_mv(uint32_t mv)
{
    static const struct {
        uint16_t mv;
        uint8_t pct;
    } curve[] = {
        {3300, 0}, {3500, 5}, {3600, 10}, {3700, 25}, {3750, 40},
        {3800, 55}, {3900, 72}, {4000, 86}, {4100, 96}, {4150, 100},
    };
    const size_t n = sizeof(curve) / sizeof(curve[0]);
    if (mv <= curve[0].mv) {
        return 0;
    }
    for (size_t i = 1; i < n; ++i) {
        if (mv <= curve[i].mv) {
            const uint32_t span = curve[i].mv - curve[i - 1].mv;
            return (uint8_t)(curve[i - 1].pct + (mv - curve[i - 1].mv) * (curve[i].pct - curve[i - 1].pct) / span);
        }
    }
    return 100;
}

esp_err_t app_power_hw_battery(app_battery_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t vbat = 0, vin = 0;
    esp_err_t err = chip();
    if (err == ESP_OK) {
        err = read_mv(PM1_REG_VBAT, &vbat);
    }
    if (err == ESP_OK) {
        err = read_mv(PM1_REG_VIN, &vin);
    }
    if (err != ESP_OK) {
        return err;
    }
    reading->percent = percent_from_mv(vbat);
    reading->voltage_mv = vbat;
    reading->current_ma = 0;
    reading->avg_current_ma = 0;
    reading->time_to_empty_min = 0xFFFFU;
    reading->remaining_mah = 0;
    reading->full_mah = 0;
    reading->external_power = vin >= VIN_PRESENT_MV;
    reading->design_mah = 0;
    return ESP_OK;
}

/* No fuel gauge here, so there is no battery profile to check or load. */
app_gauge_profile_t app_power_hw_profile(void)
{
    return APP_GAUGE_PROFILE_OK;
}
