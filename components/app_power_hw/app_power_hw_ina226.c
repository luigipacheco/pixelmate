// SPDX-License-Identifier: Apache-2.0
/* Battery monitor of the M5Stack StackChan body: an INA226 at 0x41 on the
 * CoreS3's internal I2C bus, 10 mOhm shunt (as m5stack/StackChan-BSP sets it
 * up). It gives voltage and current only, so the charge percentage is an
 * estimate from the cell voltage, and "on external power" is inferred from the
 * battery not being drained. A bare CoreS3 has no INA226: reads then fail and
 * the app shows no battery. */
#include <stddef.h>
#include "app_power_hw.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"

#define I2C_BUS "i2c_master"
#define INA226_ADDR 0x41
#define INA226_SPEED_HZ 400000
#define INA226_TIMEOUT_MS 50
#define INA226_REG_SHUNT 0x01   /* Signed, 2.5 uV per count. */
#define INA226_REG_BUS 0x02     /* 1.25 mV per count. */
/* Draining less than this (or charging) means a supply is connected: awake,
 * the head alone draws well over 100 mA from the battery. */
#define EXTERNAL_MAX_DRAIN_MA 30

/* The monitor lives on the board's bus for the app's lifetime and is owned by
 * the game loop task; the bus reference is never released. */
static i2c_master_dev_handle_t s_ina;

static esp_err_t monitor(void)
{
    if (s_ina) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = esp_board_periph_ref_handle(I2C_BUS, (void **)&bus);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_probe(bus, INA226_ADDR, INA226_TIMEOUT_MS);
    if (err == ESP_OK) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = INA226_ADDR,
            .scl_speed_hz = INA226_SPEED_HZ,
        };
        err = i2c_master_bus_add_device(bus, &config, &s_ina);
    }
    if (err != ESP_OK) {
        esp_board_periph_unref_handle(I2C_BUS);
        s_ina = NULL;
    }
    return err;
}

/* INA226 registers are big-endian 16-bit words. */
static esp_err_t read_word(uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    const esp_err_t err = i2c_master_transmit_receive(s_ina, &reg, 1, data, sizeof(data), INA226_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = (uint16_t)((data[0] << 8) | data[1]);
    }
    return err;
}

/* Resting single-cell LiPo voltage to charge, straight lines between points.
 * Reads high while charging and low under load: an estimate, not a gauge. */
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
    esp_err_t err = monitor();
    uint16_t bus = 0, shunt = 0;
    if (err == ESP_OK) {
        err = read_word(INA226_REG_BUS, &bus);
    }
    if (err == ESP_OK) {
        err = read_word(INA226_REG_SHUNT, &shunt);
    }
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t mv = (uint32_t)bus * 5U / 4U;
    /* 2.5 uV over 10 mOhm is 0.25 mA per count; the shunt reads positive
     * while the battery discharges, the app wants positive while charging. */
    const int32_t drain_ma = (int16_t)shunt / 4;
    reading->percent = percent_from_mv(mv);
    reading->voltage_mv = (uint16_t)mv;
    reading->current_ma = (int16_t)-drain_ma;
    reading->avg_current_ma = 0;
    reading->time_to_empty_min = 0xFFFFU;
    reading->remaining_mah = 0;
    reading->full_mah = 0;
    reading->external_power = drain_ma < EXTERNAL_MAX_DRAIN_MA;
    return ESP_OK;
}

/* No fuel gauge here, so there is no battery profile to check or load. */
app_gauge_profile_t app_power_hw_profile(void)
{
    return APP_GAUGE_PROFILE_OK;
}
