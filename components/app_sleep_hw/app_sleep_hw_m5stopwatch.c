// SPDX-License-Identifier: Apache-2.0
/* Pocket deep sleep for the M5Stack StopWatch (pixelmate_part(SLEEP m5stopwatch)).
 *
 * Going down: the M5IOE1 expander (I2C 0x4F on the internal bus,
 * PIXELMATE_I2C_PORT) switches off the display power (pin 8), codec power
 * (pin 3) and speaker amplifier (pin 10) and holds the display and touch in
 * reset (pins 5 and 4); the expander keeps those outputs while the ESP32-S3
 * sleeps. The BMI270 stays powered and, in its low-power mode
 * (app_pedometer_hw_low_power), keeps counting steps. Wake-up is the sleep
 * button (PIXELMATE_SLEEP_BUTTON_GPIO, active low: EXT1) or the backstop
 * timer; the board adapter powers everything up again at boot.
 *
 * Coming back: on this board nothing touches the BMI270 before the step
 * driver configures it (which resets its count), and the runtime asks for the
 * slept steps first, so app_sleep_hw_take_steps() reads the sensor's count
 * register (SC_OUT 0x1E, low 16 bits) with the normal I2C driver. */
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "app_sleep_hw.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef PIXELMATE_I2C_PORT
#define PIXELMATE_I2C_PORT 0
#endif
#ifndef PIXELMATE_SLEEP_BUTTON_GPIO
#error "the StopWatch sleep driver wakes on the board's sleep button: pixelmate_setting(SLEEP_BUTTON_GPIO ...)"
#endif

#define TAG "app_sleep"
#define IOE1_ADDR 0x4F
#define IOE1_OUT_LO 0x05            /* Output levels, pins 1-8. */
#define IOE1_OUT_HI 0x06            /* Pins 9-16. */
#define IOE1_OFF_LO 0x9C            /* Pins 3 codec, 4 touch reset, 5 display reset, 8 display power. */
#define IOE1_OFF_HI 0x02            /* Pin 10 speaker amplifier. */
#define IMU_REG_STEPS 0x1E          /* SC_OUT_0/1: low 16 bits of the step count. */
#define TIMEOUT_MS 50
#define RECORD_ARMED 0x53575031U    /* "SWP1": a sleep was started. */

const uint8_t app_sleep_probe_addrs[] = {0};
const int app_sleep_probe_count = 0;

typedef struct {
    uint32_t magic;
    uint32_t steps_before;
    uint32_t asked_s;
    int64_t slept_at_s;
    uint16_t mah_before, mv_before;
} sleep_record_t;

/* RTC memory survives deep sleep and software restarts, not a power-off.
 * Cleared as soon as this boot has read it, so a later restart is not taken
 * for another wake. */
static RTC_NOINIT_ATTR sleep_record_t s_record;
static bool s_captured, s_have_report, s_steps_taken;   /* This boot only. */
static app_sleep_report_t s_report;

static i2c_master_dev_handle_t device(uint8_t address)
{
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_master_get_bus_handle(PIXELMATE_I2C_PORT, &bus) != ESP_OK) {
        return NULL;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address, .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    return i2c_master_bus_add_device(bus, &config, &dev) == ESP_OK ? dev : NULL;
}

static esp_err_t clear_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t bits)
{
    uint8_t value = 0;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &value, 1, TIMEOUT_MS);
    if (err == ESP_OK) {
        const uint8_t data[2] = {reg, (uint8_t)(value & ~bits)};
        err = i2c_master_transmit(dev, data, sizeof(data), TIMEOUT_MS);
    }
    return err;
}

esp_err_t app_sleep_hw_enter(const app_sleep_request_t *request)
{
    if (!request || request->seconds == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const gpio_num_t button = (gpio_num_t)PIXELMATE_SLEEP_BUTTON_GPIO;
    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)request->seconds * 1000000ULL);
    if (err == ESP_OK) {
        err = esp_sleep_enable_ext1_wakeup_io(BIT64(button), ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wake sources: %s", esp_err_to_name(err));
        return err;
    }
    rtc_gpio_pullup_en(button);
    rtc_gpio_pulldown_dis(button);

    /* Display, touch and sound off through the expander. */
    i2c_master_dev_handle_t ioe = device(IOE1_ADDR);
    if (ioe) {
        if (clear_bits(ioe, IOE1_OUT_HI, IOE1_OFF_HI) != ESP_OK || clear_bits(ioe, IOE1_OUT_LO, IOE1_OFF_LO) != ESP_OK) {
            ESP_LOGW(TAG, "expander did not answer: display stays powered");
        }
        i2c_master_bus_rm_device(ioe);
    }

    memset(&s_record, 0, sizeof(s_record));
    s_record.steps_before = request->steps;
    s_record.asked_s = request->seconds;
    s_record.slept_at_s = time(NULL);
    s_record.mah_before = request->remaining_mah;
    s_record.mv_before = request->voltage_mv;
    s_record.magic = RECORD_ARMED;
    ESP_LOGI(TAG, "deep sleep: button GPIO%d or %lu s, %lu steps so far", (int)button,
             (unsigned long)request->seconds, (unsigned long)request->steps);
    vTaskDelay(pdMS_TO_TICKS(20));   /* Let the log drain. */
    esp_deep_sleep_start();
    return ESP_FAIL;   /* Not reached. */
}

/* Builds the report once per boot, reading the slept steps on a wake. */
static void capture(void)
{
    if (s_captured) {
        return;
    }
    s_captured = true;
    if (s_record.magic != RECORD_ARMED) {
        return;
    }
    s_record.magic = 0;
    app_sleep_report_t r = {
        .valid = true,
        .reset_reason = (uint8_t)esp_reset_reason(),
        .asked_s = s_record.asked_s,
        .steps_before = s_record.steps_before,
        .steps_at_wake = -1,
        .mah_before = s_record.mah_before,
        .mv_before = s_record.mv_before,
        .mask = APP_SLEEP_STEP_PANEL | APP_SLEEP_STEP_CAPTURE,
    };
    r.woke = r.reset_reason == ESP_RST_DEEPSLEEP;
    if (r.woke) {
        r.wake_causes = esp_sleep_get_wakeup_causes();
        const int64_t slept = (int64_t)time(NULL) - s_record.slept_at_s;
        r.slept_s = slept > 0 ? (uint32_t)slept : 0;
        rtc_gpio_deinit((gpio_num_t)PIXELMATE_SLEEP_BUTTON_GPIO);   /* A normal pad again. */
        i2c_master_dev_handle_t imu = device(0x68);
        const uint8_t reg = IMU_REG_STEPS;
        uint8_t steps[2] = {0};
        if (imu && i2c_master_transmit_receive(imu, &reg, 1, steps, sizeof(steps), TIMEOUT_MS) == ESP_OK) {
            r.steps_at_wake = (int32_t)(steps[0] | (steps[1] << 8));
            r.imu_chip_id = 0x24;
        }
        if (imu) {
            i2c_master_bus_rm_device(imu);
        }
    }
    s_report = r;
    s_have_report = true;
}

bool app_sleep_hw_take_steps(uint32_t *delta)
{
    capture();
    if (!s_have_report || s_steps_taken || !s_report.woke || s_report.steps_at_wake < 0) {
        return false;
    }
    s_steps_taken = true;
    const uint32_t d = ((uint32_t)s_report.steps_at_wake - s_report.steps_before) & 0xFFFFU;
    if (delta) {
        *delta = d;
    }
    return true;
}

bool app_sleep_hw_report(app_sleep_report_t *out)
{
    capture();
    if (!s_have_report) {
        return false;
    }
    if (out) {
        *out = s_report;
    }
    return true;
}
