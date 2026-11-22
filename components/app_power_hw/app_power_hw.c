// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include "app_power_hw.h"
#include "bq27220.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

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
#define GAUGE_CMD_DESIGN 0x3C

#define PROFILE_MAH 80
#define PROFILE_TASK_STACK 4096

static const char *TAG = "app_power_hw";

/* The gauge device lives on the board's bus for the app's lifetime and is
 * owned by the game loop task; the bus reference is never released (the board
 * outlives the game module). The profile task uses it only while the game loop
 * is kept off it (s_profile == BUSY). */
static i2c_master_dev_handle_t s_gauge;
static volatile app_gauge_profile_t s_profile = APP_GAUGE_PROFILE_UNCHECKED;

/* The gauge's battery profile, identical to the one the ESP-Mosaico BSP (and so
 * Vibe Mode) writes at every boot: its interim fixed-EDV profile characterized
 * on 80 mAh cells at 25 C (esp-mosaico-bsp onboard/battery.c, Apache-2.0).
 * Keeping it identical means the two firmwares never rewrite each other's. */
static const parameter_cedv_t s_cedv = {
    .full_charge_cap = PROFILE_MAH,
    .design_cap = PROFILE_MAH,
    .reserve_cap = 0,
    .near_full = 5,
    .self_discharge_rate = 20,
    .EDV0 = 3000,
    .EDV1 = 3410,
    .EDV2 = 3530,
    .EMF = 3670,
    .C0 = 115,
    .R0 = 968,
    .T0 = 4547,
    .R1 = 4764,
    .TC = 11,
    .C1 = 0,
    .DOD0 = 4147,
    .DOD10 = 4002,
    .DOD20 = 3969,
    .DOD30 = 3938,
    .DOD40 = 3880,
    .DOD50 = 3824,
    .DOD60 = 3794,
    .DOD70 = 3753,
    .DOD80 = 3677,
    .DOD90 = 3574,
    .DOD100 = 3490,
};

static const gauging_config_t s_gauging = {
    .CCT = true,
    .SC = true,
    .FCC_LIM = true,
    .FC_FOR_VDQ = true,
    .IGNORE_SD = true,
};

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

/* Load the profile through Espressif's bq27220 driver (verify, write in
 * CONFIG UPDATE mode, verify again, seal), the same path the BSP takes. */
static app_gauge_profile_t load_profile(void)
{
    i2c_master_bus_config_t *bus_cfg = NULL;
    if (esp_board_periph_get_config(I2C_BUS, (void **)&bus_cfg) != ESP_OK || !bus_cfg) {
        return APP_GAUGE_PROFILE_FAILED;
    }
    /* Wraps the board's existing bus (i2c_bus reuses an initialized port). Kept
     * for the app's lifetime: deleting it would delete the board's bus. */
    static i2c_bus_handle_t s_bridge;
    if (!s_bridge) {
        const i2c_config_t bridge_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = bus_cfg->sda_io_num,
            .scl_io_num = bus_cfg->scl_io_num,
            .sda_pullup_en = bus_cfg->flags.enable_internal_pullup,
            .scl_pullup_en = bus_cfg->flags.enable_internal_pullup,
            .master.clk_speed = GAUGE_SPEED_HZ,
        };
        s_bridge = i2c_bus_create(bus_cfg->i2c_port, &bridge_cfg);
        if (!s_bridge) {
            return APP_GAUGE_PROFILE_FAILED;
        }
    }
    const bq27220_config_t config = {.i2c_bus = s_bridge, .cfg = &s_gauging, .cedv = &s_cedv};
    bq27220_handle_t handle = bq27220_create(&config);
    if (!handle) {
        return APP_GAUGE_PROFILE_FAILED;
    }
    (void)bq27220_delete(handle);   /* Profile is in the gauge; our own reads go on. */
    return APP_GAUGE_PROFILE_LOADED;
}

static void profile_task(void *arg)
{
    (void)arg;
    app_gauge_profile_t result = APP_GAUGE_PROFILE_FAILED;
    uint16_t design = 0;
    if (gauge() == ESP_OK && read_word(GAUGE_CMD_DESIGN, &design) == ESP_OK) {
        if (design == PROFILE_MAH) {
            result = APP_GAUGE_PROFILE_OK;
        } else {
            ESP_LOGW(TAG, "gauge reset to defaults (design %u mAh): loading the %u mAh profile", design,
                     PROFILE_MAH);
            result = load_profile();
            if (result == APP_GAUGE_PROFILE_LOADED && read_word(GAUGE_CMD_DESIGN, &design) == ESP_OK) {
                ESP_LOGI(TAG, "gauge profile loaded: design %u mAh", design);
            }
        }
    }
    if (result == APP_GAUGE_PROFILE_FAILED) {
        ESP_LOGE(TAG, "gauge profile check failed; battery %% uses the gauge defaults");
    }
    s_profile = result;   /* Hands the gauge back to the game loop. */
    vTaskDelete(NULL);
}

app_gauge_profile_t app_power_hw_profile(void)
{
    return s_profile;
}

esp_err_t app_power_hw_battery(app_battery_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_profile == APP_GAUGE_PROFILE_UNCHECKED) {
        /* Checking can take a few seconds when it has to load (the driver
         * waits for the gauge to seal), so it runs beside the game loop. */
        s_profile = APP_GAUGE_PROFILE_BUSY;
        if (xTaskCreate(profile_task, "gauge_profile", PROFILE_TASK_STACK, NULL, tskIDLE_PRIORITY + 1, NULL)
            != pdPASS) {
            s_profile = APP_GAUGE_PROFILE_FAILED;
        }
    }
    if (s_profile == APP_GAUGE_PROFILE_BUSY) {
        return ESP_ERR_NOT_FINISHED;
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
    /* Extras for power measurements; a failed one is reported as unknown. */
    uint16_t avg = 0, tte = 0, rem = 0, full = 0, design = 0;
    reading->avg_current_ma = read_word(GAUGE_CMD_AVG_CURRENT, &avg) == ESP_OK ? (int16_t)avg : 0;
    reading->time_to_empty_min = read_word(GAUGE_CMD_TIME_TO_EMPTY, &tte) == ESP_OK ? tte : 0xFFFFU;
    reading->remaining_mah = read_word(GAUGE_CMD_REMAINING, &rem) == ESP_OK ? rem : 0;
    reading->full_mah = read_word(GAUGE_CMD_FULL, &full) == ESP_OK ? full : 0;
    reading->design_mah = read_word(GAUGE_CMD_DESIGN, &design) == ESP_OK ? design : 0;
    return ESP_OK;
}
