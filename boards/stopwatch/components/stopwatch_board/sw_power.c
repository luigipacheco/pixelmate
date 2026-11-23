// SPDX-License-Identifier: Apache-2.0
/* M5Stack StopWatch power-up: the internal I2C bus and the two M5 chips on it.
 *
 * The display, touch and audio rails and resets are not ESP32 GPIOs: they hang
 * off the M5PM1 power chip (0x6E) and the M5IOE1 I/O expander (0x4F). The
 * sequence and register values are M5GFX's StopWatch power-on
 * (src/board_detect/m5/pmic_ops.hpp, stopwatch_power_on; MIT, M5Stack):
 *
 *   PM1:  0x09 = 0, 0x0A = 0 (I2C config, watchdog off); 0x06 |= 0x17 (rails)
 *   GPIO39 (display CS) high
 *   wait for the IOE1 to answer (it boots after the PM1), up to 500 ms
 *   IOE1: 0x23 = 0; push-pull (0x13 &= ~0x9D) and outputs (0x03 |= 0x9D) for
 *         pins 1, 3, 4, 5, 8; 0x05 |= 0x99: pin 1 USB switch on, pin 8 display
 *         power on, pins 4/5 (touch/display reset) high, pin 3 codec power off;
 *         pulse the resets low 8 ms; pin 10 (speaker amp) output, off.
 *
 * IOE1 registers (from that sequence and M5Unified's M5IOE1_Class): 0x03/0x04
 * direction (1 = output) for pins 1-8 / 9-16, 0x05/0x06 output level,
 * 0x13/0x14 drive mode (1 = open drain). Pin N is bit (N-1) % 8. */
#include "sw_power.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "sw_power"
#define I2C_TIMEOUT_MS 50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_pm1, s_ioe;

static esp_err_t write8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t read8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t update(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t set, uint8_t clear)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(read8(dev, reg, &value), TAG, "read 0x%02x", reg);
    return write8(dev, reg, (uint8_t)((value & ~clear) | set));
}

static esp_err_t add_device(uint8_t addr, uint32_t hz, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    return i2c_master_bus_add_device(s_bus, &config, out);
}

esp_err_t sw_power_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = SW_I2C_PORT,
        .sda_io_num = SW_I2C_SDA,
        .scl_io_num = SW_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(add_device(SW_PM1_ADDR, 100000, &s_pm1), TAG, "PM1");
    ESP_RETURN_ON_ERROR(add_device(SW_IOE1_ADDR, 100000, &s_ioe), TAG, "IOE1");

    /* PM1: I2C config and watchdog to defaults, then the rails. */
    ESP_RETURN_ON_ERROR(write8(s_pm1, 0x09, 0x00), TAG, "PM1 I2C config");
    ESP_RETURN_ON_ERROR(write8(s_pm1, 0x0A, 0x00), TAG, "PM1 watchdog");
    ESP_RETURN_ON_ERROR(update(s_pm1, 0x06, 0x17, 0), TAG, "PM1 rails");

    /* Display chip select idles high while the panel powers up. */
    gpio_set_direction(SW_LCD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(SW_LCD_CS, 1);

    /* The IOE1 boots after the PM1. */
    esp_err_t probe = ESP_FAIL;
    for (int waited = 0; waited < 500 && probe != ESP_OK; waited += 10) {
        probe = i2c_master_probe(s_bus, SW_IOE1_ADDR, 10);
        if (probe != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ESP_RETURN_ON_ERROR(probe, TAG, "IOE1 did not answer");

    ESP_RETURN_ON_ERROR(write8(s_ioe, 0x23, 0x00), TAG, "IOE1 0x23");
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x13, 0, 0x9D), TAG, "IOE1 push-pull");
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x03, 0x9D, 0), TAG, "IOE1 outputs");
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x05, 0x99, 0), TAG, "IOE1 power on");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x05, 0, 0x18), TAG, "IOE1 resets low");
    vTaskDelay(pdMS_TO_TICKS(8));
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x05, 0x18, 0), TAG, "IOE1 resets high");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x14, 0, 0x02), TAG, "IOE1 amp push-pull");
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x04, 0x02, 0), TAG, "IOE1 amp output");
    ESP_RETURN_ON_ERROR(update(s_ioe, 0x06, 0, 0x02), TAG, "IOE1 amp off");
    ESP_LOGI(TAG, "display and touch powered");
    return ESP_OK;
}

i2c_master_bus_handle_t sw_power_bus(void)
{
    return s_bus;
}
