// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include <sys/time.h>
#include "app_sleep_hw.h"
#include "app_sleep_wake_stub.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_board_manager_includes.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_private/startup_internal.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "app_sleep";

/* Pins, as in the BSP (esp_mosaico.h) and the engine's board adapter. */
#define RAIL_GPIO GPIO_NUM_60          /* VCC_3V3 switch, active low. */
#define RAIL_ON 0
#define RAIL_OFF 1
#define CODEC_RAIL_GPIO GPIO_NUM_56    /* v1.0 only, active high; I2C SDA on later boards. */
#define POWER_SWITCH_GPIO GPIO_NUM_57  /* Shutdown line: must stay released (high-Z). */
#define LCD_CS_GPIO GPIO_NUM_50
#define TOUCH_INT_GPIO GPIO_NUM_6      /* An LP pad (GPIO0..7), so it can wake the chip. */
/* The AI button, an LP pad too. Held low at boot it sends the Vibe Mode
 * bootloader into recovery, so its wake goes through the wake stub
 * (rtc_wake_stub_ai_button.c), which waits for the release first. */
#define BUTTON_GPIO ((gpio_num_t)APP_WAKE_STUB_GPIO)

#define I2C_BUS "i2c_master"
#define PROBE_TIMEOUT_MS 20
#define DRAIN_MS 250                   /* Let the last frame leave before talking to the panel. */
#define IMU_REG_CHIP_ID 0x00
#define IMU_REG_STEPS 0x1E             /* SC_OUT_0/1: low 16 bits of the step count. */
#define IMU_REG_STATUS 0x21

/* IMU (both straps), fuel gauge, magnetometers, codec, EEPROM-style slots. */
const uint8_t app_sleep_probe_addrs[] = {0x68, 0x69, 0x55, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x5A, 0x1A};
const int app_sleep_probe_count = sizeof(app_sleep_probe_addrs) / sizeof(app_sleep_probe_addrs[0]);

#define RECORD_ARMED 0x534C5031U       /* "SLP1": a sleep was started. */
#define RECORD_REPORT 0x534C5032U      /* "SLP2": its result is in `report`. */
#define RECORD_CAPTURING 0x534C5033U   /* "SLP3": the capture started and never finished. */
#define RECORD_ARMED_PLAIN 0x534C5034U /* "SLP4": a plain sleep (no sensor capture on wake). */

/* Flash breadcrumb: survives a power cycle, unlike RTC memory. */
#define CRUMB_NS "sleeptest"
#define CRUMB_KEY "crumb"
typedef struct {
    uint32_t magic;                    /* CRUMB_MAGIC while a sleep is outstanding. */
    uint32_t seconds;
    uint8_t plain, rail_off, touch_wake, mask;
    uint8_t pin57, pin60, button_wake, reserved;
    int64_t slept_at_s;
} sleep_crumb_t;
#define CRUMB_MAGIC 0x43524D31U         /* "CRM1". */

typedef struct {
    uint32_t magic;
    app_sleep_request_t request;
    int64_t slept_at_s;                /* Wall clock (RTC backed) when we went down. */
    uint16_t alive_rail_off;
    bool v1_0;
    bool steps_taken;                  /* Slept steps already credited. */
    app_sleep_report_t report;
} sleep_record_t;

/* RTC memory survives deep sleep and software restarts, not a power-off. */
static RTC_NOINIT_ATTR sleep_record_t s_record;

static bool board_is_v1_0(void)
{
    uint16_t version = 0;
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &version, sizeof(version) * 8U) != ESP_OK) {
        return false;
    }
    return version == 0x0100;
}

static int64_t wall_seconds(void)
{
    struct timeval now = {0};
    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec;
}

/* ---- Flash breadcrumb ------------------------------------------------------ */

static void write_crumb(const app_sleep_request_t *request, uint8_t mask, bool touch_wake, bool button_wake)
{
    const sleep_crumb_t crumb = {
        .magic = CRUMB_MAGIC, .seconds = request->seconds, .plain = mask == 0, .mask = mask,
        .rail_off = request->rail_off, .touch_wake = touch_wake, .button_wake = button_wake,
        .slept_at_s = wall_seconds(),
        .pin57 = (uint8_t)gpio_get_level(POWER_SWITCH_GPIO), .pin60 = (uint8_t)gpio_get_level(RAIL_GPIO),
    };
    nvs_handle_t nvs;
    if (nvs_open(CRUMB_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_blob(nvs, CRUMB_KEY, &crumb, sizeof(crumb));
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static bool take_crumb(sleep_crumb_t *out)
{
    nvs_handle_t nvs;
    if (nvs_open(CRUMB_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*out);
    const bool found = nvs_get_blob(nvs, CRUMB_KEY, out, &size) == ESP_OK && size == sizeof(*out)
                       && out->magic == CRUMB_MAGIC;
    (void)nvs_erase_key(nvs, CRUMB_KEY);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
    return found;
}

static void clear_crumb(void)
{
    sleep_crumb_t unused;
    (void)take_crumb(&unused);
}

/* ---- Going down ----------------------------------------------------------- */

static void panel_standby(void)
{
    dev_display_lcd_handles_t *lcd = NULL;
    if (esp_board_manager_get_device_handle("display_lcd", (void **)&lcd) != ESP_OK || !lcd || !lcd->panel_handle) {
        ESP_LOGW(TAG, "no display handle: panel left as it is");
        return;
    }
    /* Display off, Sleep In, then Deep Standby (the CO5300 driver sends DSTBON
     * after SLPIN). Only a reset pulse wakes the panel: the restart does that. */
    esp_err_t err = esp_lcd_panel_disp_on_off(lcd->panel_handle, false);
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_sleep(lcd->panel_handle, true);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel standby: %s", esp_err_to_name(err));
    }
    /* SPI still drives CS high; a driven pad leaks into a sleeping panel. */
    gpio_reset_pin(LCD_CS_GPIO);
    const gpio_config_t floating = {
        .pin_bit_mask = BIT64(LCD_CS_GPIO), .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&floating);
}

/* Which I2C devices still answer (run right after the rail is cut). */
static uint16_t probe_bus(void)
{
    i2c_master_bus_handle_t bus = NULL;
    if (esp_board_periph_ref_handle(I2C_BUS, (void **)&bus) != ESP_OK || !bus) {
        return 0;
    }
    uint16_t alive = 0;
    for (int i = 0; i < app_sleep_probe_count; ++i) {
        if (i2c_master_probe(bus, app_sleep_probe_addrs[i], PROBE_TIMEOUT_MS) == ESP_OK) {
            alive |= (uint16_t)(1U << i);
        }
    }
    esp_board_periph_unref_handle(I2C_BUS);
    return alive;
}

esp_err_t app_sleep_hw_enter(const app_sleep_request_t *request)
{
    if (!request || request->seconds == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool v1_0 = board_is_v1_0();
    const uint8_t mask = request->plain ? 0 : request->mask;
    const bool touch_wake = request->touch_wake && !request->rail_off;
    const bool button_wake = request->button_wake;
    if (button_wake && gpio_get_level(BUTTON_GPIO) == 0) {
        return ESP_ERR_INVALID_STATE;   /* Held: it would wake at once. Retry after the release. */
    }
    ESP_LOGI(TAG, "deep sleep for %lu s, steps 0x%x, rail %s, touch wake %s, button wake %s, board %s",
             (unsigned long)request->seconds, mask, request->rail_off ? "off" : "on", touch_wake ? "on" : "off",
             button_wake ? "on" : "off", v1_0 ? "v1.0" : "v1.1+");
    vTaskDelay(pdMS_TO_TICKS(DRAIN_MS));
    write_crumb(request, mask, touch_wake, button_wake);
    if (mask & APP_SLEEP_STEP_PANEL) {
        panel_standby();
    }

    uint16_t alive = 0;
    if (request->rail_off) {
        gpio_set_level(RAIL_GPIO, RAIL_OFF);
        if (v1_0) {
            gpio_set_level(CODEC_RAIL_GPIO, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        alive = probe_bus();
        ESP_LOGI(TAG, "rail off: I2C alive mask 0x%04x", alive);
    }

    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)request->seconds * 1000000ULL);
    /* EXT1 pins, all active low: the touch interrupt (low while a finger is
     * down) and the AI button. */
    const uint64_t ext1 = (touch_wake ? BIT64(TOUCH_INT_GPIO) : 0) | (button_wake ? BIT64(BUTTON_GPIO) : 0);
    if (err == ESP_OK && ext1) {
        err = esp_sleep_enable_ext1_wakeup_io(ext1, ESP_EXT1_WAKEUP_ANY_LOW);
        for (int pin = 0; err == ESP_OK && pin < GPIO_NUM_8; ++pin) {
            if (ext1 & BIT64(pin)) {
                rtc_gpio_pullup_en((gpio_num_t)pin);
                rtc_gpio_pulldown_dis((gpio_num_t)pin);
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wake sources: %s", esp_err_to_name(err));
        if (request->rail_off) {
            gpio_set_level(RAIL_GPIO, RAIL_ON);   /* Not sleeping after all. */
        }
        clear_crumb();
        return err;
    }
    /* Keep the power pins where they are while the chip sleeps. */
    if (mask & APP_SLEEP_STEP_HOLD_RAIL) {
        gpio_hold_en(RAIL_GPIO);
        if (v1_0) {
            gpio_hold_en(CODEC_RAIL_GPIO);
        }
    }
    if (mask & APP_SLEEP_STEP_HOLD_SWITCH) {
        gpio_hold_en(POWER_SWITCH_GPIO);
    }

    memset(&s_record, 0, sizeof(s_record));
    s_record.request = *request;
    s_record.request.touch_wake = touch_wake;
    s_record.request.button_wake = button_wake;
    s_record.slept_at_s = wall_seconds();
    s_record.alive_rail_off = alive;
    s_record.v1_0 = v1_0;
    s_record.request.mask = mask;
    s_record.magic = (mask & APP_SLEEP_STEP_CAPTURE) ? RECORD_ARMED : RECORD_ARMED_PLAIN;
    if (button_wake) {
        app_wake_stub_arm();
    }
    esp_deep_sleep_start();
    return ESP_FAIL;   /* Not reached. */
}

/* ---- Coming back ---------------------------------------------------------- */

/* The capture below runs before the scheduler starts, so the I2C driver (it
 * blocks on semaphores) is out: read the sensor with a tiny bit-banged bus. */
typedef struct {
    gpio_num_t sda, scl;
} bb_bus_t;

#define BB_HALF_US 10   /* ~50 kHz. */

static void bb_pin(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);   /* Open drain: 1 releases the line. */
    esp_rom_delay_us(BB_HALF_US);
}

static void bb_start(const bb_bus_t *bus)
{
    bb_pin(bus->sda, 1);
    bb_pin(bus->scl, 1);
    bb_pin(bus->sda, 0);
    bb_pin(bus->scl, 0);
}

static void bb_stop(const bb_bus_t *bus)
{
    bb_pin(bus->sda, 0);
    bb_pin(bus->scl, 1);
    bb_pin(bus->sda, 1);
}

/* Returns true when the device acknowledged. */
static bool bb_write(const bb_bus_t *bus, uint8_t byte)
{
    for (int bit = 7; bit >= 0; --bit) {
        bb_pin(bus->sda, (byte >> bit) & 1);
        bb_pin(bus->scl, 1);
        bb_pin(bus->scl, 0);
    }
    bb_pin(bus->sda, 1);
    bb_pin(bus->scl, 1);
    const bool ack = gpio_get_level(bus->sda) == 0;
    bb_pin(bus->scl, 0);
    return ack;
}

static uint8_t bb_read(const bb_bus_t *bus, bool ack)
{
    uint8_t byte = 0;
    bb_pin(bus->sda, 1);
    for (int bit = 0; bit < 8; ++bit) {
        bb_pin(bus->scl, 1);
        byte = (uint8_t)((byte << 1) | (gpio_get_level(bus->sda) ? 1 : 0));
        bb_pin(bus->scl, 0);
    }
    bb_pin(bus->sda, ack ? 0 : 1);
    bb_pin(bus->scl, 1);
    bb_pin(bus->scl, 0);
    bb_pin(bus->sda, 1);
    return byte;
}

static bool imu_read(const bb_bus_t *bus, uint8_t address, uint8_t reg, uint8_t *data, size_t count)
{
    bb_start(bus);
    bool ok = bb_write(bus, (uint8_t)(address << 1)) && bb_write(bus, reg);
    if (ok) {
        bb_start(bus);
        ok = bb_write(bus, (uint8_t)((address << 1) | 1U));
        for (size_t i = 0; ok && i < count; ++i) {
            data[i] = bb_read(bus, i + 1 < count);
        }
    }
    bb_stop(bus);
    return ok;
}

/* Read the BMI270's raw step count BEFORE the board brings the sensor up
 * again (its init resets the chip and the count). */
static bool imu_snapshot(bool v1_0, app_sleep_report_t *report)
{
    const bb_bus_t bus = {
        .sda = v1_0 ? GPIO_NUM_0 : GPIO_NUM_56,
        .scl = v1_0 ? GPIO_NUM_1 : GPIO_NUM_3,
    };
    const gpio_config_t lines = {
        .pin_bit_mask = BIT64(bus.sda) | BIT64(bus.scl), .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(bus.sda, 1);
    gpio_set_level(bus.scl, 1);
    if (gpio_config(&lines) != ESP_OK) {
        return false;
    }
    esp_rom_delay_us(200);
    bool found = false;
    for (int i = 0; i < 2 && !found; ++i) {
        const uint8_t address = i == 0 ? 0x69 : 0x68;
        uint8_t id = 0, steps[2] = {0}, status = 0;
        if (imu_read(&bus, address, IMU_REG_CHIP_ID, &id, 1) && imu_read(&bus, address, IMU_REG_STEPS, steps, 2)
                && imu_read(&bus, address, IMU_REG_STATUS, &status, 1)) {
            report->imu_chip_id = id;
            report->imu_status = status;
            report->steps_at_wake = (int32_t)(steps[0] | (steps[1] << 8));
            found = true;
        }
    }
    gpio_reset_pin(bus.sda);   /* The board's I2C driver sets them up again. */
    gpio_reset_pin(bus.scl);
    return found;
}

/* Runs before app_main and before the board starts: the only moment the
 * sensor still holds what it counted while we slept. Does nothing on a
 * normal boot. */
ESP_SYSTEM_INIT_FN(app_sleep_capture, SECONDARY, BIT(0), 250)
{
    if (s_record.magic != RECORD_ARMED && s_record.magic != RECORD_ARMED_PLAIN) {
        return ESP_OK;
    }
    const bool plain = s_record.magic == RECORD_ARMED_PLAIN;
    const sleep_record_t armed = s_record;
    /* One try only: if anything below hangs or crashes, the next boot must
     * start normally and not come back here. */
    s_record.magic = RECORD_CAPTURING;
    app_sleep_report_t report = {
        .valid = true,
        .reset_reason = (uint8_t)esp_reset_reason(),
        .rail_off = armed.request.rail_off,
        .touch_wake = armed.request.touch_wake,
        .button_wake = armed.request.button_wake,
        .v1_0 = armed.v1_0,
        .asked_s = armed.request.seconds,
        .steps_before = armed.request.steps,
        .steps_at_wake = -1,
        .alive_rail_off = armed.alive_rail_off,
        .mah_before = armed.request.remaining_mah,
        .mv_before = armed.request.voltage_mv,
        .plain = armed.request.mask == 0,
        .mask = armed.request.mask,
    };
    report.woke = report.reset_reason == ESP_RST_DEEPSLEEP;
    if (armed.request.touch_wake) {
        rtc_gpio_deinit(TOUCH_INT_GPIO);   /* Back to a normal pad for the touch driver. */
    }
    if (armed.request.button_wake) {
        /* The stub already handed the pad back; this also resets the RTC IO
         * driver's view of it (see commit ab42d91: an RTC pad reads nothing). */
        rtc_gpio_deinit(BUTTON_GPIO);
        app_wake_stub_result_t stub;
        if (app_wake_stub_take(&stub)) {
            report.button_confirmed = stub.confirmed;
            report.stub_runs = stub.runs;
            report.stub_resleeps = stub.resleeps;
        }
    }
    if (report.woke) {
        report.wake_causes = esp_sleep_get_wakeup_causes();
        report.ext1_pins = (uint8_t)(esp_sleep_get_ext1_wakeup_status() & 0xFFU);
        const int64_t slept = wall_seconds() - armed.slept_at_s;
        report.slept_s = slept > 0 ? (uint32_t)slept : 0;
    }
    if (report.woke && !plain) {
        /* First with the rail as the sleep left it; when nothing answers and
         * the rail was cut, power it (the board does the same next) and retry. */
        report.imu_answered_rail_off = armed.request.rail_off && imu_snapshot(armed.v1_0, &report);
        if (!report.imu_answered_rail_off) {
            if (armed.request.rail_off) {
                gpio_set_level(RAIL_GPIO, RAIL_ON);
                gpio_set_direction(RAIL_GPIO, GPIO_MODE_OUTPUT);
                gpio_hold_dis(RAIL_GPIO);
                esp_rom_delay_us(30000);
            }
            (void)imu_snapshot(armed.v1_0, &report);
        }
    }
    if ((armed.request.mask & APP_SLEEP_STEP_HOLD_RAIL) && !armed.request.rail_off) {
        /* Release the rail pin's hold, still driving it on, so the board can
         * take it over without the rail (and the step sensor) blinking off. */
        gpio_set_level(RAIL_GPIO, RAIL_ON);
        gpio_set_direction(RAIL_GPIO, GPIO_MODE_OUTPUT);
        gpio_hold_dis(RAIL_GPIO);
    }
    s_record.report = report;
    s_record.magic = RECORD_REPORT;
    return ESP_OK;
}

bool app_sleep_hw_take_steps(uint32_t *delta)
{
    if (s_record.magic != RECORD_REPORT || s_record.steps_taken || !s_record.report.woke
            || s_record.report.steps_at_wake < 0) {
        return false;
    }
    s_record.steps_taken = true;
    /* The sensor register holds the low 16 bits of its count. */
    const uint32_t d = ((uint32_t)s_record.report.steps_at_wake - s_record.report.steps_before) & 0xFFFFU;
    if (delta) {
        *delta = d;
    }
    return true;
}

bool app_sleep_hw_report(app_sleep_report_t *out)
{
    sleep_crumb_t crumb;
    const bool had_crumb = take_crumb(&crumb);
    if (s_record.magic == RECORD_REPORT && s_record.report.valid) {
        if (out) {
            *out = s_record.report;
        }
        return true;
    }
    if (!had_crumb) {
        return false;
    }
    /* The RTC record is gone (power cycle, or a reset that cleared RTC
     * memory): the sleep was started and never came back as a wake. */
    const app_sleep_report_t report = {
        .valid = true, .woke = false, .from_crumb = true, .plain = crumb.plain != 0, .mask = crumb.mask,
        .pin57 = crumb.pin57, .pin60 = crumb.pin60,
        .rail_off = crumb.rail_off != 0, .touch_wake = crumb.touch_wake != 0, .button_wake = crumb.button_wake != 0,
        .reset_reason = (uint8_t)esp_reset_reason(), .asked_s = crumb.seconds, .steps_at_wake = -1,
    };
    ESP_LOGW(TAG, "breadcrumb: a sleep (steps 0x%x) of %lu s never woke; pins 57/60 were %u/%u; "
             "this boot's reset reason %u", crumb.mask, (unsigned long)crumb.seconds, crumb.pin57, crumb.pin60,
             report.reset_reason);
    if (out) {
        *out = report;
    }
    return true;
}
