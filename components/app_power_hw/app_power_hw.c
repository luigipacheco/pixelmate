// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include "app_power_hw.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"

#define DISPLAY_DEVICE "display_lcd"
#define I2C_BUS "i2c_master"
#define GAUGE_ADDR 0x55
#define GAUGE_SPEED_HZ 400000
#define GAUGE_TIMEOUT_MS 50
/* BQ27220 standard commands: little-endian 16-bit words. */
#define GAUGE_CMD_VOLTAGE 0x08
#define GAUGE_CMD_CURRENT 0x0C
#define GAUGE_CMD_SOC 0x2C

/* Both handles are borrowed from the board for the app's lifetime and owned
 * by the game loop task; the bus reference is never released (the board
 * outlives the game module). */
static esp_lcd_panel_handle_t s_panel;
static i2c_master_dev_handle_t s_gauge;
static bool s_display_on = true;

static esp_lcd_panel_handle_t panel(void)
{
    if (!s_panel) {
        void *handle = NULL;
        if (esp_board_manager_get_device_handle(DISPLAY_DEVICE, &handle) == ESP_OK && handle) {
            s_panel = ((dev_display_lcd_handles_t *)handle)->panel_handle;
        }
    }
    return s_panel;
}

esp_err_t app_power_hw_display(bool on, uint8_t brightness_pct)
{
    esp_lcd_panel_handle_t p = panel();
    if (!p) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!on) {
        /* Brightness 0 first: on this AMOLED "display off" alone can leave the
         * last picture visible, which looks like a frozen screen. */
        const esp_err_t dim = esp_lcd_panel_co5300_set_brightness(p, 0);
        s_display_on = false;
        const esp_err_t off = esp_lcd_panel_disp_on_off(p, false);
        return off != ESP_OK ? off : dim;
    }
    if (!s_display_on) {
        const esp_err_t err = esp_lcd_panel_disp_on_off(p, true);
        if (err != ESP_OK) {
            return err;
        }
        s_display_on = true;
    }
    return esp_lcd_panel_co5300_set_brightness(p, brightness_pct ? brightness_pct : 1);
}

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
    return ESP_OK;
}
