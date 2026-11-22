// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include "app_power_hw.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"

#define I2C_BUS "i2c_master"
#define GAUGE_ADDR 0x55
#define GAUGE_SPEED_HZ 400000
#define GAUGE_TIMEOUT_MS 50
/* BQ27220 standard commands: little-endian 16-bit words. */
#define GAUGE_CMD_VOLTAGE 0x08
#define GAUGE_CMD_CURRENT 0x0C
#define GAUGE_CMD_SOC 0x2C
#define GAUGE_CMD_REMAINING 0x10
#define GAUGE_CMD_FULL 0x12
#define GAUGE_CMD_AVG_CURRENT 0x14
#define GAUGE_CMD_TIME_TO_EMPTY 0x16

/* The gauge device lives on the board's bus for the app's lifetime and is
 * owned by the game loop task; the bus reference is never released (the board
 * outlives the game module). */
static i2c_master_dev_handle_t s_gauge;

static esp_err_t gauge(void)
{
    if (s_gauge) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = esp_board_periph_ref_handle(I2C_BUS, (void **)&bus);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_ADDR,
        .scl_speed_hz = GAUGE_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(bus, &config, &s_gauge);
    if (err != ESP_OK) {
        esp_board_periph_unref_handle(I2C_BUS);
        s_gauge = NULL;
    }
    return err;
}

static esp_err_t read_word(uint8_t command, uint16_t *value)
{
    uint8_t data[2] = {0};
    const esp_err_t err = i2c_master_transmit_receive(s_gauge, &command, 1, data, sizeof(data),
                                                      GAUGE_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = (uint16_t)(data[0] | (data[1] << 8));
    }
    return err;
}

esp_err_t app_power_hw_battery(app_battery_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = gauge();
    uint16_t soc = 0, mv = 0, ma = 0;
    if (err == ESP_OK) {
        err = read_word(GAUGE_CMD_SOC, &soc);
    }
    if (err == ESP_OK) {
        err = read_word(GAUGE_CMD_VOLTAGE, &mv);
    }
    if (err == ESP_OK) {
        err = read_word(GAUGE_CMD_CURRENT, &ma);
    }
    if (err != ESP_OK) {
        return err;
    }
    reading->percent = (uint8_t)(soc > 100 ? 100 : soc);
    reading->voltage_mv = mv;
    reading->current_ma = (int16_t)ma;
    reading->external_power = false;   /* Not used on this board (pocket mode reads the current). */
    /* Extras for power measurements; a failed one is reported as unknown. */
    uint16_t avg = 0, tte = 0, rem = 0, full = 0;
    reading->avg_current_ma = read_word(GAUGE_CMD_AVG_CURRENT, &avg) == ESP_OK ? (int16_t)avg : 0;
    reading->time_to_empty_min = read_word(GAUGE_CMD_TIME_TO_EMPTY, &tte) == ESP_OK ? tte : 0xFFFFU;
    reading->remaining_mah = read_word(GAUGE_CMD_REMAINING, &rem) == ESP_OK ? rem : 0;
    reading->full_mah = read_word(GAUGE_CMD_FULL, &full) == ESP_OK ? full : 0;
    return ESP_OK;
}
