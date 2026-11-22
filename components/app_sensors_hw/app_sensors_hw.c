// SPDX-License-Identifier: Apache-2.0
#include "app_sensors_hw.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_rom_sys.h"

#define I2C_BUS "i2c_master"

enum {
    REG_CHIP_ID = 0x40, CHIP_ID = 0x32,
    REG_DATA = 0x42,      /* X LSB .. Z MSB, then RHALL. */
    REG_POWER = 0x4B,     /* bit0: power control. */
    REG_OPMODE = 0x4C,    /* bits 2:1 = 00 normal; bits 5:3 data rate (000 = 10 Hz). */
    I2C_TIMEOUT_MS = 20,
};

static const uint8_t s_addr[APP_MAGNETOMETERS] = {0x11, 0x12};
static i2c_master_dev_handle_t s_dev[APP_MAGNETOMETERS];
static bool s_ok[APP_MAGNETOMETERS];

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

int app_sensors_hw_mag_start(void)
{
    i2c_master_bus_handle_t bus = NULL;
    if (esp_board_periph_ref_handle(I2C_BUS, (void **)&bus) != ESP_OK || !bus) {
        return 0;
    }
    int found = 0;
    for (int i = 0; i < APP_MAGNETOMETERS; ++i) {
        if (s_ok[i]) {
            ++found;
            continue;
        }
        if (!s_dev[i]) {
            const i2c_device_config_t config = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = s_addr[i],
                .scl_speed_hz = 400000,
            };
            if (i2c_master_bus_add_device(bus, &config, &s_dev[i]) != ESP_OK) {
                s_dev[i] = NULL;
                continue;
            }
        }
        if (write_reg(s_dev[i], REG_POWER, 0x01) != ESP_OK) {
            continue;
        }
        esp_rom_delay_us(3000);   /* Start-up time after power-on. */
        uint8_t reg = REG_CHIP_ID, id = 0;
        if (i2c_master_transmit_receive(s_dev[i], &reg, 1, &id, 1, I2C_TIMEOUT_MS) != ESP_OK || id != CHIP_ID) {
            continue;
        }
        if (write_reg(s_dev[i], REG_OPMODE, 0x00) != ESP_OK) {   /* Normal mode, 10 Hz. */
            continue;
        }
        s_ok[i] = true;
        ++found;
    }
    return found;
}

esp_err_t app_sensors_hw_mag_read(int index, int16_t xyz[3])
{
    if (index < 0 || index >= APP_MAGNETOMETERS || !xyz) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ok[index]) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = REG_DATA, d[6];
    const esp_err_t err = i2c_master_transmit_receive(s_dev[index], &reg, 1, d, sizeof(d), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    /* X and Y are 13-bit (bits 15:3), Z is 15-bit (bits 15:1), two's complement. */
    xyz[0] = (int16_t)((int16_t)(d[1] << 8 | (d[0] & 0xF8)) >> 3);
    xyz[1] = (int16_t)((int16_t)(d[3] << 8 | (d[2] & 0xF8)) >> 3);
    xyz[2] = (int16_t)((int16_t)(d[5] << 8 | (d[4] & 0xFE)) >> 1);
    return ESP_OK;
}
