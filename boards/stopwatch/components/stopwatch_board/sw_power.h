// SPDX-License-Identifier: Apache-2.0
#pragma once
/* M5Stack StopWatch pins and power-up (see sw_power.c). */
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/* Internal I2C bus: PM1 0x6E, IOE1 0x4F, touch 0x15 (or 0x5A), ES8311 0x18,
 * RX8130 RTC 0x32, BMI270 0x68. Shared parts drivers find it by port number
 * (i2c_master_get_bus_handle). */
#define SW_I2C_PORT 1
#define SW_I2C_SDA 47
#define SW_I2C_SCL 48
#define SW_PM1_ADDR 0x6E
#define SW_IOE1_ADDR 0x4F

/* CO5300 AMOLED over QSPI. */
#define SW_LCD_CS 39
#define SW_LCD_SCLK 40
#define SW_LCD_D0 41
#define SW_LCD_D1 42
#define SW_LCD_D2 46
#define SW_LCD_D3 45
#define SW_LCD_TE 38
#define SW_TOUCH_INT 13

/** Open the internal I2C bus and power the display and touch (once). */
esp_err_t sw_power_init(void);
i2c_master_bus_handle_t sw_power_bus(void);
/** Drive an M5IOE1 output pin (1-16; already set up as an output in
 * sw_power_init: 3 codec power, 10 speaker amplifier). */
esp_err_t sw_power_ioe_set(int pin, bool on);
#define SW_IOE_CODEC_POWER 3
#define SW_IOE_SPEAKER_AMP 10
