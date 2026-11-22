// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "app_interact_hw.h"
#include "app_camera_hw.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rmt_tx.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "app_interact";

/* Right-slot pins (BSP subboard map of the module's left-slot pins). */
#define PIN_ADDR_SELECT GPIO_NUM_39   /* EEPROM A0: high -> 0x51. */
#define PIN_LDR GPIO_NUM_46
#define PIN_IR GPIO_NUM_47
#define PIN_KEY_L GPIO_NUM_11
#define PIN_KEY_R GPIO_NUM_10
#define PIN_PIR GPIO_NUM_5
#define PIN_WS2812 GPIO_NUM_38

enum {
    EEPROM_ADDR = 0x51,
    BOARD_TYPE_INTERACT = 0x16,
    I2C_TIMEOUT_MS = 50,
    MISSES_TO_DROP = 3,
    LDR_ZERO = 2196, LDR_HIGH = 4393,   /* S31 nominal ADC codes (BSP). */
};

static struct {
    bool present, up;
    uint8_t misses;
    adc_oneshot_unit_handle_t adc;
    adc_channel_t ldr_channel;
    int light_q8;
    bool light_valid;
    rmt_channel_handle_t led_chan, ir_chan;
    rmt_encoder_handle_t led_enc, ir_enc;
} s;

static bool probe(void)
{
    void *bus = NULL;
    if (app_camera_hw_slot_bus(&bus) != ESP_OK || !bus) {
        return false;
    }
    gpio_set_direction(PIN_ADDR_SELECT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ADDR_SELECT, 1);
    if (i2c_master_probe((i2c_master_bus_handle_t)bus, EEPROM_ADDR, I2C_TIMEOUT_MS) != ESP_OK) {
        return false;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = EEPROM_ADDR, .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t eeprom = NULL;
    if (i2c_master_bus_add_device((i2c_master_bus_handle_t)bus, &config, &eeprom) != ESP_OK) {
        return false;
    }
    const uint8_t offset = 0;
    uint8_t head[4] = {0};
    const esp_err_t err = i2c_master_transmit_receive(eeprom, &offset, 1, head, sizeof(head), I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(eeprom);
    return err == ESP_OK && memcmp(head, "ESP", 3) == 0 && head[3] == BOARD_TYPE_INTERACT;
}

static void release(void)
{
    if (s.led_chan) {
        rmt_disable(s.led_chan);
        rmt_del_channel(s.led_chan);
        s.led_chan = NULL;
    }
    if (s.led_enc) {
        rmt_del_encoder(s.led_enc);
        s.led_enc = NULL;
    }
    if (s.ir_chan) {
        rmt_disable(s.ir_chan);
        rmt_del_channel(s.ir_chan);
        s.ir_chan = NULL;
    }
    if (s.ir_enc) {
        rmt_del_encoder(s.ir_enc);
        s.ir_enc = NULL;
    }
    if (s.adc) {
        adc_oneshot_del_unit(s.adc);
        s.adc = NULL;
    }
    s.up = false;
    s.light_valid = false;
}

static esp_err_t bring_up(void)
{
    const gpio_config_t keys = {
        .pin_bit_mask = BIT64(PIN_KEY_L) | BIT64(PIN_KEY_R), .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    const gpio_config_t pir = {.pin_bit_mask = BIT64(PIN_PIR), .mode = GPIO_MODE_INPUT};
    esp_err_t err = gpio_config(&keys);
    if (err == ESP_OK) err = gpio_config(&pir);

    adc_unit_t unit;
    if (err == ESP_OK) err = adc_oneshot_io_to_channel(PIN_LDR, &unit, &s.ldr_channel);
    if (err == ESP_OK) {
        const adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit};
        err = adc_oneshot_new_unit(&ucfg, &s.adc);
    }
    if (err == ESP_OK) {
#if SOC_ADC_ATTEN_NUM <= 1
        const adc_oneshot_chan_cfg_t ccfg = {.atten = ADC_ATTEN_DB_0, .bitwidth = ADC_BITWIDTH_DEFAULT};
#else
        const adc_oneshot_chan_cfg_t ccfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
#endif
        err = adc_oneshot_config_channel(s.adc, s.ldr_channel, &ccfg);
    }

    /* WS2812: 10 MHz RMT, bytes encoder with the 0.3/0.9 us bit shapes. */
    if (err == ESP_OK) {
        const rmt_tx_channel_config_t led = {
            .gpio_num = PIN_WS2812, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
            .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL, .trans_queue_depth = 2,
        };
        err = rmt_new_tx_channel(&led, &s.led_chan);
    }
    if (err == ESP_OK) {
        const rmt_bytes_encoder_config_t enc = {
            .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
            .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
            .flags.msb_first = 1,
        };
        err = rmt_new_bytes_encoder(&enc, &s.led_enc);
    }
    if (err == ESP_OK) err = rmt_enable(s.led_chan);

    /* IR: 1 MHz RMT with a 38 kHz carrier, copy encoder for NEC symbols. */
    if (err == ESP_OK) {
        const rmt_tx_channel_config_t ir = {
            .gpio_num = PIN_IR, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 1000000,
            .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL, .trans_queue_depth = 2,
        };
        err = rmt_new_tx_channel(&ir, &s.ir_chan);
    }
    if (err == ESP_OK) {
        const rmt_carrier_config_t carrier = {.frequency_hz = 38000, .duty_cycle = 0.33f};
        err = rmt_apply_carrier(s.ir_chan, &carrier);
    }
    if (err == ESP_OK) {
        const rmt_copy_encoder_config_t copy = {};
        err = rmt_new_copy_encoder(&copy, &s.ir_enc);
    }
    if (err == ESP_OK) err = rmt_enable(s.ir_chan);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bring-up failed: %s", esp_err_to_name(err));
        release();
        return err;
    }
    s.up = true;
    ESP_LOGI(TAG, "interaction module ready in the right slot");
    return ESP_OK;
}

bool app_interact_hw_poll(void)
{
    if (app_camera_hw_present() && !s.present) {
        /* The camera's probing owns the slot pins while it streams; the right
         * slot is still ours, so carry on. */
    }
    const bool here = probe();
    if (here) {
        s.misses = 0;
        if (!s.present) {
            s.present = bring_up() == ESP_OK;
        }
    } else if (s.present && ++s.misses >= MISSES_TO_DROP) {
        ESP_LOGI(TAG, "interaction module removed");
        release();
        s.present = false;
    }
    return s.present;
}

bool app_interact_hw_present(void)
{
    return s.present && s.up;
}

esp_err_t app_interact_hw_read(app_interact_inputs_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_interact_hw_present()) {
        return ESP_ERR_INVALID_STATE;
    }
    out->left = gpio_get_level(PIN_KEY_L) == 0;
    out->right = gpio_get_level(PIN_KEY_R) == 0;
    out->motion = gpio_get_level(PIN_PIR) != 0;
    int raw = 0;
    if (adc_oneshot_read(s.adc, s.ldr_channel, &raw) == ESP_OK) {
        /* Even channels are N-side inputs on the S31: codes fall as light rises. */
        const bool inverted = (s.ldr_channel & 1U) == 0;
        int level = inverted ? LDR_ZERO - raw : raw - LDR_ZERO;
        const int span = inverted ? LDR_ZERO : LDR_HIGH - LDR_ZERO;
        level = level < 0 ? 0 : (level > span ? span : level);
        const int target = level * 100 * 256 / span;
        s.light_q8 = s.light_valid ? s.light_q8 + (target - s.light_q8) / 8 : target;
        s.light_valid = true;
    }
    out->light = (uint8_t)((s.light_q8 + 128) / 256);
    return ESP_OK;
}

esp_err_t app_interact_hw_leds(const uint8_t rgb[APP_INTERACT_LEDS][3])
{
    if (!app_interact_hw_present() || !rgb) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t grb[APP_INTERACT_LEDS * 3];
    for (int i = 0; i < APP_INTERACT_LEDS; ++i) {
        grb[i * 3] = rgb[i][1];
        grb[i * 3 + 1] = rgb[i][0];
        grb[i * 3 + 2] = rgb[i][2];
    }
    const rmt_transmit_config_t tx = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s.led_chan, s.led_enc, grb, sizeof(grb), &tx);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s.led_chan, 20);   /* Then >50 us low latches. */
    }
    return err;
}

static void nec_bit(rmt_symbol_word_t *sym, bool one)
{
    *sym = (rmt_symbol_word_t) {.level0 = 1, .duration0 = 560, .level1 = 0, .duration1 = one ? 1690 : 560};
}

esp_err_t app_interact_hw_ir_nec(uint8_t address, uint8_t command)
{
    if (!app_interact_hw_present()) {
        return ESP_ERR_INVALID_STATE;
    }
    rmt_symbol_word_t sym[34];
    size_t n = 0;
    sym[n++] = (rmt_symbol_word_t) {.level0 = 1, .duration0 = 9000, .level1 = 0, .duration1 = 4500};
    const uint16_t words[2] = {
        (uint16_t)(address | ((uint16_t)(uint8_t)~address << 8)),
        (uint16_t)(command | ((uint16_t)(uint8_t)~command << 8)),
    };
    for (int w = 0; w < 2; ++w) {
        for (int bit = 0; bit < 16; ++bit) {
            nec_bit(&sym[n++], (words[w] >> bit) & 1U);
        }
    }
    sym[n++] = (rmt_symbol_word_t) {.level0 = 1, .duration0 = 560, .level1 = 0, .duration1 = 560};
    const rmt_transmit_config_t tx = {.loop_count = 0};
    esp_err_t err = rmt_transmit(s.ir_chan, s.ir_enc, sym, n * sizeof(sym[0]), &tx);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s.ir_chan, 120);
    }
    return err;
}
