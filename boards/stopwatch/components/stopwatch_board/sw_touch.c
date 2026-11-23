// SPDX-License-Identifier: Apache-2.0
/* StopWatch touch: CST820 at 0x15 (CST816S register layout, as M5GFX drives
 * it). It reports 0..233 across the round glass, half the panel's 468 visible
 * pixels, and the glass starts 6 columns into the 480-wide panel memory
 * (M5GFX: touch range 0..233, panel 468x468 at offset 6,0). Some units carry
 * a two-finger CST9217 at 0x5A instead; that one is not supported yet.
 * Coordinates come out in panel pixels; sw_video maps them onto the game. */
#include "sw_touch.h"

#include "esp_check.h"
#include "esp_log.h"
#include "sw_power.h"

#define TAG "sw_touch"
#define CST820_ADDR 0x15
#define CST9217_ADDR 0x5A
#define TIMEOUT_MS 20
#define RAW_SPAN 234      /* Raw 0..233. */
#define GLASS 468         /* Visible pixels. */
#define GLASS_X0 6        /* First visible column of the 480-wide memory. */
#define PANEL 480

static i2c_master_dev_handle_t s_touch;

esp_err_t sw_touch_init(void)
{
    i2c_master_bus_handle_t bus = sw_power_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "power not up");
    /* The controller wakes a few ms after its reset is released. */
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 8 && err != ESP_OK; ++i) {
        err = i2c_master_probe(bus, CST820_ADDR, 10);
        if (err != ESP_OK && i2c_master_probe(bus, CST9217_ADDR, 10) == ESP_OK) {
            ESP_LOGW(TAG, "CST9217 touch (0x5A) found: not supported yet, touch is off");
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ESP_RETURN_ON_ERROR(err, TAG, "CST820 did not answer");
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CST820_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_touch), TAG, "add CST820");
    /* Keep it awake: asleep it stops answering until touched. */
    const uint8_t no_auto_sleep[2] = {0xFE, 0x01};
    (void)i2c_master_transmit(s_touch, no_auto_sleep, sizeof(no_auto_sleep), TIMEOUT_MS);
    ESP_LOGI(TAG, "CST820 touch ready");
    return ESP_OK;
}

static int32_t to_logical(int32_t raw, int32_t first)
{
    int32_t v = first + raw * GLASS / RAW_SPAN;
    return v < 0 ? 0 : (v >= PANEL ? PANEL - 1 : v);
}

bool sw_touch_read(int32_t *x, int32_t *y)
{
    if (!s_touch) {
        return false;
    }
    /* 0x02 finger count, 0x03-0x04 X (12 bits), 0x05-0x06 Y. */
    const uint8_t reg = 0x02;
    uint8_t data[5] = {0};
    if (i2c_master_transmit_receive(s_touch, &reg, 1, data, sizeof(data), TIMEOUT_MS) != ESP_OK) {
        return false;
    }
    static bool s_down;
    if ((data[0] & 0x0F) == 0) {
        s_down = false;
        return false;
    }
    const int32_t rx = ((data[1] & 0x0F) << 8) | data[2];
    const int32_t ry = ((data[3] & 0x0F) << 8) | data[4];
    *x = to_logical(rx, GLASS_X0);
    *y = to_logical(ry, 0);
    if (!s_down) {
        /* Calibration aid: one line per touch-down (raw chip value, panel pixel). */
        ESP_LOGI(TAG, "down raw %ld,%ld -> panel %ld,%ld", (long)rx, (long)ry, (long)*x, (long)*y);
        s_down = true;
    }
    return true;
}
