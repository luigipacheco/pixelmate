// SPDX-License-Identifier: Apache-2.0
/* StopWatch speaker: ES8311 codec (I2C 0x18) into an AW8737A amplifier.
 * Codec power is M5IOE1 pin 3, the amplifier enable pin 10. The codec setup
 * is M5Unified's StopWatch speaker sequence (MIT, M5Stack): MCLK taken from
 * BCLK x8, DAC on, +6 dB. I2S0: MCLK 18, BCLK 17, WS 15, data to codec 21;
 * 16-bit stereo, the game's mono samples sent to both slots. */
#include "platform_esp_audio.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sw_power.h"

#define TAG "sw_audio"
#define ES8311_ADDR 0x18
#define I2C_TIMEOUT_MS 50
#define CHUNK_FRAMES 256U

static i2s_chan_handle_t s_tx;
static i2c_master_dev_handle_t s_codec;
static bool s_running;
static int16_t s_stereo[CHUNK_FRAMES * 2];

static esp_err_t codec_write(const uint8_t *pairs)
{
    for (; pairs[0] || pairs[1]; pairs += 2) {
        const uint8_t data[2] = {pairs[0], pairs[1]};
        const esp_err_t err = i2c_master_transmit(s_codec, data, sizeof(data), I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static raylib_lite_result_t audio_start(void *context, const raylib_lite_audio_format_t *format)
{
    (void)context;
    if (!format || format->sample_rate != RAYLIB_LITE_AUDIO_SAMPLE_RATE ||
            format->channels != RAYLIB_LITE_AUDIO_CHANNELS || format->format != RAYLIB_LITE_PCM_S16_NATIVE)
        return RAYLIB_LITE_NOT_SUPPORTED;
    if (s_running) return RAYLIB_LITE_INVALID_STATE;
    i2c_master_bus_handle_t bus = sw_power_bus();
    if (!bus) return RAYLIB_LITE_NOT_READY;
    if (!s_codec) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ES8311_ADDR, .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bus, &config, &s_codec) != ESP_OK) return RAYLIB_LITE_NOT_READY;
    }
    if (!s_tx) {
        i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        chan.auto_clear = true;
        if (i2s_new_channel(&chan, &s_tx, NULL) != ESP_OK) return RAYLIB_LITE_NOT_READY;
        const i2s_std_config_t std = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RAYLIB_LITE_AUDIO_SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = GPIO_NUM_18, .bclk = GPIO_NUM_17, .ws = GPIO_NUM_15,
                .dout = GPIO_NUM_21, .din = I2S_GPIO_UNUSED,
            },
        };
        if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) return RAYLIB_LITE_NOT_READY;
    }
    if (i2s_channel_enable(s_tx) != ESP_OK) return RAYLIB_LITE_NOT_READY;
    /* Codec power (left on afterwards: cutting it forces a codec reset). */
    if (sw_power_ioe_set(SW_IOE_CODEC_POWER, true) != ESP_OK) return RAYLIB_LITE_NOT_READY;
    vTaskDelay(pdMS_TO_TICKS(10));
    static const uint8_t on[] = {
        0x00, 0x80,   /* Reset off, chip state machine on. */
        0x01, 0xB5,   /* Clock manager: MCLK from BCLK. */
        0x02, 0x18,   /* Pre-multiply x8. */
        0x0D, 0x01,   /* Analog power up. */
        0x12, 0x00,   /* DAC power up. */
        0x13, 0x10,   /* Output to the headphone/line driver. */
        0x32, 0xCB,   /* DAC volume +6 dB (0xBF = 0 dB). */
        0x37, 0x08,   /* Bypass the DAC equalizer. */
        0x00, 0x00,
    };
    if (codec_write(on) != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 did not answer");
        return RAYLIB_LITE_NOT_READY;
    }
    sw_power_ioe_set(SW_IOE_SPEAKER_AMP, true);
    s_running = true;
    ESP_LOGI(TAG, "ES8311 playback started: %u Hz", (unsigned)RAYLIB_LITE_AUDIO_SAMPLE_RATE);
    return RAYLIB_LITE_OK;
}

static raylib_lite_result_t audio_write(void *context, const int16_t *frames, size_t frame_count,
                                        uint32_t timeout_ms, size_t *out_written)
{
    (void)context;
    if (!out_written || (!frames && frame_count)) return RAYLIB_LITE_INVALID_ARGUMENT;
    *out_written = 0;
    if (!s_running) return RAYLIB_LITE_NOT_READY;
    while (*out_written < frame_count) {
        const size_t n = frame_count - *out_written < CHUNK_FRAMES ? frame_count - *out_written : CHUNK_FRAMES;
        for (size_t i = 0; i < n; ++i) {
            s_stereo[2 * i] = s_stereo[2 * i + 1] = frames[*out_written + i];
        }
        size_t bytes = 0;
        if (i2s_channel_write(s_tx, s_stereo, n * 2U * sizeof(int16_t), &bytes,
                              timeout_ms ? timeout_ms : portMAX_DELAY) != ESP_OK)
            return *out_written ? RAYLIB_LITE_OK : RAYLIB_LITE_TIMEOUT;
        *out_written += bytes / (2U * sizeof(int16_t));
        if (bytes < n * 2U * sizeof(int16_t)) break;
    }
    return RAYLIB_LITE_OK;
}

static raylib_lite_result_t audio_stop(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    if (!s_running) return RAYLIB_LITE_OK;
    sw_power_ioe_set(SW_IOE_SPEAKER_AMP, false);
    static const uint8_t off[] = {0x12, 0x02, 0x00, 0x00};   /* DAC power down. */
    (void)codec_write(off);
    i2s_channel_disable(s_tx);
    s_running = false;
    return RAYLIB_LITE_OK;
}

raylib_lite_audio_backend_t platform_esp_audio_board_backend(void)
{
    return (raylib_lite_audio_backend_t) {
        .context = NULL, .start = audio_start, .write = audio_write, .stop = audio_stop,
    };
}
