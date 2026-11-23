// SPDX-License-Identifier: Apache-2.0
/* M5Stack StackChan neck: two Feetech SCS0009 bus servos on UART1 (1 Mbps,
 * TX 6 / RX 7; id 1 = pan, id 2 = tilt). Protocol, power sequence and the
 * motion filter follow the fab-stackchan firmware that first ran on this
 * robot; the pins and the power switch are the ones m5stack/StackChan-BSP uses.
 *
 * Things that are easy to get wrong here:
 * - The servo rail is switched by a PY32 IO expander (I2C 0x6F, pin 0 VM_EN).
 *   The pin is open-drain: without its pull-up "high" floats and the servos
 *   neither move nor answer. The expander also boots slowly.
 * - SCS registers are big-endian. Goal time and speed stay 0: the filter below
 *   is the motion profile.
 * - The tilt axis must never be driven into its stops. Every write is clamped
 *   to a calibrated band, the head must be found inside that band before
 *   anything moves, and a servo that stops following is released. */
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "app_neck_hw.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "neck";

#define I2C_BUS "i2c_master"
#define PY32_ADDR 0x6F
#define PY32_SPEED_HZ 100000
#define PY32_TIMEOUT_MS 50
#define PY32_REG_VERSION 0x02
#define PY32_REG_MODE 0x03          /* Bit per pin, 1 = output. */
#define PY32_REG_OUTPUT 0x05
#define PY32_REG_PULL_UP 0x09
#define PY32_REG_PULL_DOWN 0x0B
#define PY32_VM_EN 0x01             /* Pin 0: servo rail enable. */

#define SCS_UART UART_NUM_1
#define SCS_TX_GPIO 6
#define SCS_RX_GPIO 7
#define SCS_BAUD 1000000
#define SCS_ID_PAN 1
#define SCS_ID_TILT 2
#define SCS_INST_READ 0x02
#define SCS_INST_WRITE 0x03
#define SCS_REG_TORQUE 0x28
#define SCS_REG_GOAL 0x2A           /* Position, time, speed: 3 big-endian words. */
#define SCS_REG_POSITION 0x38
#define SCS_TICKS_PER_DEG 3.41f     /* 1024 ticks over ~300 degrees. */

/* Calibration measured on Luis's robot by the earlier firmware ("tilt range",
 * read back from its NVS): home is per robot, the mechanism does not assemble
 * to the same neutral twice. NVS namespace "neck" (u16 keys below) overrides. */
#define DEFAULT_HOME_PAN 512
#define DEFAULT_HOME_TILT 743
#define DEFAULT_TILT_LO 592         /* Usable tilt band in ticks, stops excluded. */
#define DEFAULT_TILT_HI 894
/* Tick direction of a positive aim. Inferred from the earlier firmware (its
 * "sag" pose raised the tilt ticks); confirm on the robot. */
#define PAN_TICK_SIGN 1.0f
#define TILT_TICK_SIGN (-1.0f)

/* Spring-damper motion (f Hz, damping z, response r): the earlier firmware's
 * feel, slightly calmer start. */
#define MOTION_F 1.15f
#define MOTION_Z 0.75f
#define MOTION_R 0.60f

enum {
    STEP_MS = 20,                   /* 50 Hz. */
    RAIL_SETTLE_MS = 200,
    PY32_BOOT_TRIES = 6,            /* The expander answers late after power-up. */
    PY32_BOOT_WAIT_MS = 200,
    READ_TRIES = 4,
    REPLY_WAIT_MS = 4,
    BAND_SLACK_TICKS = 15,          /* Head may rest this far outside the band. */
    CHECK_EVERY_STEPS = 10,         /* One position read every 200 ms, axes alternate. */
    STALL_TICKS = 50,               /* Goal and reported position this far apart... */
    STALL_CHECKS = 5,               /* ...on this many checks in a row (2 s) = stalled. */
    REST_SETTLE_STEPS = 40,         /* Home reached for 0.8 s before torque is released. */
    RETRY_MS = 5000,
    MAX_ATTEMPTS = 4,               /* Then one retry a minute. */
    RETRY_SLOW_MS = 60000,
    TASK_STACK = 4096,
    TASK_PRIORITY = 4,
};

typedef struct {
    float xp, y, yd, k1, k2, k3;
} motion_t;

/* Shared with callers. */
static _Atomic int s_state = APP_NECK_ABSENT;
static _Atomic int s_aim_pan_cdeg, s_aim_tilt_cdeg;   /* Centidegrees. */
static _Atomic bool s_rest;
static TaskHandle_t s_task;

/* Worker only. */
static struct {
    i2c_master_dev_handle_t py32;
    bool uart_ready, rail_on;
    uint16_t home_pan, home_tilt, tilt_lo, tilt_hi;
    motion_t pan, tilt;
    uint16_t goal_pan, goal_tilt;   /* Last ticks written. */
    uint8_t stall_pan, stall_tilt;
    uint32_t steps, settled;
} s_n;

static void set_state(app_neck_state_t state)
{
    if (atomic_exchange(&s_state, (int)state) != (int)state) {
        ESP_LOGI(TAG, "state %d", (int)state);
    }
}

/* ---- Servo rail (PY32 IO expander) ------------------------------------ */

static esp_err_t py32_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_n.py32, &reg, 1, value, 1, PY32_TIMEOUT_MS);
}

static esp_err_t py32_bit(uint8_t reg, uint8_t mask, bool on)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(py32_read(reg, &value), TAG, "expander read 0x%02x", reg);
    const uint8_t data[2] = {reg, (uint8_t)(on ? (value | mask) : (value & ~mask))};
    return i2c_master_transmit(s_n.py32, data, sizeof(data), PY32_TIMEOUT_MS);
}

static esp_err_t rail_on(void)
{
    if (s_n.rail_on) {
        return ESP_OK;
    }
    if (!s_n.py32) {
        i2c_master_bus_handle_t bus = NULL;
        ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(I2C_BUS, (void **)&bus), TAG, "board i2c");
        esp_err_t err = i2c_master_probe(bus, PY32_ADDR, PY32_TIMEOUT_MS);
        if (err == ESP_OK) {
            const i2c_device_config_t config = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = PY32_ADDR,
                .scl_speed_hz = PY32_SPEED_HZ,
            };
            err = i2c_master_bus_add_device(bus, &config, &s_n.py32);
        }
        if (err != ESP_OK) {
            esp_board_periph_unref_handle(I2C_BUS);
            s_n.py32 = NULL;
            ESP_LOGW(TAG, "no IO expander at 0x%02x: not a StackChan body?", PY32_ADDR);
            return ESP_ERR_NOT_FOUND;
        }
    }
    /* Early writes are acknowledged and dropped until the expander has booted:
     * wait for a real version byte first. */
    uint8_t version = 0;
    for (int i = 0; i < PY32_BOOT_TRIES; ++i) {
        if (py32_read(PY32_REG_VERSION, &version) == ESP_OK && version != 0x00 && version != 0xFF) {
            break;
        }
        version = 0;
        vTaskDelay(pdMS_TO_TICKS(PY32_BOOT_WAIT_MS));
    }
    ESP_RETURN_ON_FALSE(version, ESP_ERR_TIMEOUT, TAG, "IO expander not ready");
    ESP_RETURN_ON_ERROR(py32_bit(PY32_REG_MODE, PY32_VM_EN, true), TAG, "direction");
    ESP_RETURN_ON_ERROR(py32_bit(PY32_REG_PULL_DOWN, PY32_VM_EN, false), TAG, "pull-down");
    ESP_RETURN_ON_ERROR(py32_bit(PY32_REG_PULL_UP, PY32_VM_EN, true), TAG, "pull-up");
    ESP_RETURN_ON_ERROR(py32_bit(PY32_REG_OUTPUT, PY32_VM_EN, true), TAG, "output");
    vTaskDelay(pdMS_TO_TICKS(RAIL_SETTLE_MS));
    s_n.rail_on = true;
    ESP_LOGI(TAG, "servo rail on (expander v%u)", version);
    return ESP_OK;
}

/* ---- SCS bus ----------------------------------------------------------- */

static esp_err_t bus_open(void)
{
    if (s_n.uart_ready) {
        return ESP_OK;
    }
    const uart_config_t config = {
        .baud_rate = SCS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    /* One install on the documented pins; never re-opened on other pins (that
     * left TX unrouted in the earlier firmware). */
    ESP_RETURN_ON_ERROR(uart_driver_install(SCS_UART, 256, 0, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(SCS_UART, &config), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(SCS_UART, SCS_TX_GPIO, SCS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart pins");
    s_n.uart_ready = true;
    return ESP_OK;
}

/* FF FF ID LEN INST PARAMS... CHK, LEN = params + 2, CHK = ~(ID + LEN + INST + params). */
static size_t scs_packet(uint8_t *out, uint8_t id, uint8_t inst, const uint8_t *params, uint8_t count)
{
    uint8_t sum = (uint8_t)(id + count + 2 + inst);
    out[0] = out[1] = 0xFF;
    out[2] = id;
    out[3] = (uint8_t)(count + 2);
    out[4] = inst;
    for (uint8_t i = 0; i < count; ++i) {
        out[5 + i] = params[i];
        sum = (uint8_t)(sum + params[i]);
    }
    out[5 + count] = (uint8_t)~sum;
    return (size_t)count + 6;
}

static void scs_send(uint8_t id, uint8_t inst, const uint8_t *params, uint8_t count)
{
    uint8_t packet[16];
    const size_t n = scs_packet(packet, id, inst, params, count);
    uart_write_bytes(SCS_UART, packet, n);
}

static void scs_torque(uint8_t id, bool on)
{
    const uint8_t params[2] = {SCS_REG_TORQUE, on ? 1 : 0};
    scs_send(id, SCS_INST_WRITE, params, sizeof(params));
}

static void scs_goal(uint8_t id, uint16_t ticks)
{
    const uint8_t params[7] = {SCS_REG_GOAL, (uint8_t)(ticks >> 8), (uint8_t)ticks, 0, 0, 0, 0};
    scs_send(id, SCS_INST_WRITE, params, sizeof(params));
}

/* Read the present position. The reply is FF FF ID 04 ERR PosH PosL CHK; a
 * bus that echoes the request delivers that first (same length), so a packet
 * identical to the request is skipped. */
static esp_err_t scs_position(uint8_t id, uint16_t *ticks)
{
    const uint8_t params[2] = {SCS_REG_POSITION, 2};
    uint8_t request[8], reply[8];
    const size_t n = scs_packet(request, id, SCS_INST_READ, params, sizeof(params));
    uart_flush_input(SCS_UART);
    uart_write_bytes(SCS_UART, request, n);
    uart_wait_tx_done(SCS_UART, pdMS_TO_TICKS(REPLY_WAIT_MS));
    for (int pass = 0; pass < 2; ++pass) {
        if (uart_read_bytes(SCS_UART, reply, sizeof(reply), pdMS_TO_TICKS(REPLY_WAIT_MS)) != (int)sizeof(reply)) {
            return ESP_ERR_TIMEOUT;
        }
        if (memcmp(reply, request, sizeof(reply)) == 0) {
            continue;   /* Our own request, echoed. */
        }
        const uint8_t sum = (uint8_t)~(reply[2] + reply[3] + reply[4] + reply[5] + reply[6]);
        if (reply[0] != 0xFF || reply[1] != 0xFF || reply[2] != id || reply[3] != 4 || reply[7] != sum) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *ticks = (uint16_t)((reply[5] << 8) | reply[6]);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t scs_position_retry(uint8_t id, uint16_t *ticks)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < READ_TRIES && err != ESP_OK; ++i) {
        err = scs_position(id, ticks);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return err;
}

/* ---- Motion ------------------------------------------------------------ */

static void motion_init(motion_t *m, float x0)
{
    const float w = 2.0f * (float)M_PI * MOTION_F;
    m->k1 = MOTION_Z / ((float)M_PI * MOTION_F);
    m->k2 = 1.0f / (w * w);
    m->k3 = MOTION_R * MOTION_Z / w;
    m->xp = m->y = x0;
    m->yd = 0.0f;
}

/* Second-order dynamics (semi-implicit Euler, stable for any step). */
static float motion_update(motion_t *m, float dt, float x)
{
    const float xd = (x - m->xp) / dt;
    m->xp = x;
    float k2 = m->k2;
    if (dt * dt / 2.0f + dt * m->k1 / 2.0f > k2) {
        k2 = dt * dt / 2.0f + dt * m->k1 / 2.0f;
    }
    if (dt * m->k1 > k2) {
        k2 = dt * m->k1;
    }
    m->y += dt * m->yd;
    m->yd += dt * (x + m->k3 * xd - m->y - m->k1 * m->yd) / k2;
    return m->y;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t pan_ticks(float deg)
{
    const float span = APP_NECK_PAN_MAX_DEG * SCS_TICKS_PER_DEG;
    return (uint16_t)clampf((float)s_n.home_pan + PAN_TICK_SIGN * deg * SCS_TICKS_PER_DEG,
                            clampf((float)s_n.home_pan - span, 0.0f, 1023.0f),
                            clampf((float)s_n.home_pan + span, 0.0f, 1023.0f));
}

/* Clamped twice: to the aim limit around home and to the calibrated band. */
static uint16_t tilt_ticks(float deg)
{
    const float span = APP_NECK_TILT_MAX_DEG * SCS_TICKS_PER_DEG;
    const float lo = clampf((float)s_n.home_tilt - span, (float)s_n.tilt_lo, (float)s_n.tilt_hi);
    const float hi = clampf((float)s_n.home_tilt + span, (float)s_n.tilt_lo, (float)s_n.tilt_hi);
    return (uint16_t)clampf((float)s_n.home_tilt + TILT_TICK_SIGN * deg * SCS_TICKS_PER_DEG, lo, hi);
}

static void load_calibration(void)
{
    s_n.home_pan = DEFAULT_HOME_PAN;
    s_n.home_tilt = DEFAULT_HOME_TILT;
    s_n.tilt_lo = DEFAULT_TILT_LO;
    s_n.tilt_hi = DEFAULT_TILT_HI;
    nvs_handle_t h;
    if (nvs_open("neck", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u16(h, "home_pan", &s_n.home_pan);
        nvs_get_u16(h, "home_tilt", &s_n.home_tilt);
        nvs_get_u16(h, "tilt_lo", &s_n.tilt_lo);
        nvs_get_u16(h, "tilt_hi", &s_n.tilt_hi);
        nvs_close(h);
    }
    if (s_n.tilt_lo >= s_n.tilt_hi || s_n.home_tilt < s_n.tilt_lo || s_n.home_tilt > s_n.tilt_hi) {
        ESP_LOGW(TAG, "bad stored calibration; using defaults");
        s_n.home_tilt = DEFAULT_HOME_TILT;
        s_n.tilt_lo = DEFAULT_TILT_LO;
        s_n.tilt_hi = DEFAULT_TILT_HI;
    }
}

static void release(void)
{
    if (s_n.uart_ready) {
        scs_torque(SCS_ID_PAN, false);
        scs_torque(SCS_ID_TILT, false);
    }
}

/* Find the head, check it is where the calibration says a head can be, then
 * take hold exactly there: nothing jumps. */
static esp_err_t take_hold(void)
{
    uint16_t pan = 0, tilt = 0;
    ESP_RETURN_ON_ERROR(scs_position_retry(SCS_ID_PAN, &pan), TAG, "pan servo silent");
    ESP_RETURN_ON_ERROR(scs_position_retry(SCS_ID_TILT, &tilt), TAG, "tilt servo silent");
    ESP_LOGI(TAG, "head at pan %u tilt %u ticks (home %u/%u, tilt band %u..%u)", pan, tilt, s_n.home_pan,
             s_n.home_tilt, s_n.tilt_lo, s_n.tilt_hi);
    ESP_RETURN_ON_FALSE(tilt + BAND_SLACK_TICKS >= s_n.tilt_lo && tilt <= s_n.tilt_hi + BAND_SLACK_TICKS,
                        ESP_ERR_INVALID_STATE, TAG, "tilt is outside the calibrated band: recalibrate before moving");
    motion_init(&s_n.pan, PAN_TICK_SIGN * ((float)pan - (float)s_n.home_pan) / SCS_TICKS_PER_DEG);
    motion_init(&s_n.tilt, TILT_TICK_SIGN * ((float)tilt - (float)s_n.home_tilt) / SCS_TICKS_PER_DEG);
    s_n.goal_pan = pan;
    s_n.goal_tilt = tilt;
    s_n.stall_pan = s_n.stall_tilt = 0;
    s_n.settled = 0;
    scs_goal(SCS_ID_PAN, pan);       /* Goal = where it is, before torque. */
    scs_goal(SCS_ID_TILT, tilt);
    scs_torque(SCS_ID_PAN, true);
    scs_torque(SCS_ID_TILT, true);
    return ESP_OK;
}

static esp_err_t bring_up(void)
{
    ESP_RETURN_ON_ERROR(rail_on(), TAG, "rail");
    ESP_RETURN_ON_ERROR(bus_open(), TAG, "bus");
    load_calibration();
    return take_hold();
}

/* True when the servo has not been following its goal for STALL_CHECKS checks. */
static bool stalled(uint8_t id, uint16_t goal, uint8_t *count)
{
    uint16_t now = 0;
    if (scs_position(id, &now) != ESP_OK) {
        return false;   /* A missed read is not a stall. */
    }
    const int error = (int)now - (int)goal;
    *count = (uint8_t)((error > STALL_TICKS || error < -STALL_TICKS) ? *count + 1 : 0);
    return *count >= STALL_CHECKS;
}

/* One 50 Hz step while holding. Returns false when the neck must stop. */
static bool step(void)
{
    const bool rest = atomic_load(&s_rest);
    const float dt = STEP_MS / 1000.0f;
    const float aim_pan = rest ? 0.0f : clampf(atomic_load(&s_aim_pan_cdeg) / 100.0f, -APP_NECK_PAN_MAX_DEG,
                          APP_NECK_PAN_MAX_DEG);
    const float aim_tilt = rest ? 0.0f : clampf(atomic_load(&s_aim_tilt_cdeg) / 100.0f, -APP_NECK_TILT_MAX_DEG,
                           APP_NECK_TILT_MAX_DEG);
    const float pan = motion_update(&s_n.pan, dt, aim_pan);
    const float tilt = motion_update(&s_n.tilt, dt, aim_tilt);
    s_n.goal_pan = pan_ticks(pan);
    s_n.goal_tilt = tilt_ticks(tilt);
    scs_goal(SCS_ID_PAN, s_n.goal_pan);
    scs_goal(SCS_ID_TILT, s_n.goal_tilt);
    if (++s_n.steps % CHECK_EVERY_STEPS == 0) {
        const bool tilt_turn = (s_n.steps / CHECK_EVERY_STEPS) % 2U;
        if (tilt_turn ? stalled(SCS_ID_TILT, s_n.goal_tilt, &s_n.stall_tilt)
                : stalled(SCS_ID_PAN, s_n.goal_pan, &s_n.stall_pan)) {
            ESP_LOGE(TAG, "%s servo is not following: released", tilt_turn ? "tilt" : "pan");
            return false;
        }
    }
    if (rest) {
        const bool home = fabsf(pan) < 0.7f && fabsf(tilt) < 0.7f && fabsf(s_n.pan.yd) < 2.0f && fabsf(s_n.tilt.yd) < 2.0f;
        s_n.settled = home ? s_n.settled + 1 : 0;
        if (s_n.settled >= REST_SETTLE_STEPS) {
            release();
            set_state(APP_NECK_RESTING);
        }
    } else {
        s_n.settled = 0;
    }
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    int attempts = 0;
    bool holding = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (!holding) {
            if (atomic_load(&s_state) != APP_NECK_ERROR) {
                set_state(APP_NECK_STARTING);
            }
            const esp_err_t err = bring_up();
            if (err != ESP_OK) {
                release();
                set_state(APP_NECK_ERROR);
                ESP_LOGW(TAG, "start: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(++attempts >= MAX_ATTEMPTS ? RETRY_SLOW_MS : RETRY_MS));
                continue;
            }
            attempts = 0;
            holding = true;
            set_state(APP_NECK_READY);
            last = xTaskGetTickCount();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(STEP_MS));
        if (atomic_load(&s_state) == APP_NECK_RESTING) {
            if (!atomic_load(&s_rest)) {
                /* Wake: the head may have been moved by hand meanwhile. */
                if (take_hold() == ESP_OK) {
                    set_state(APP_NECK_READY);
                } else {
                    release();
                    set_state(APP_NECK_ERROR);
                    holding = false;
                    vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
                }
            }
            continue;
        }
        if (!step()) {
            release();
            set_state(APP_NECK_ERROR);
            holding = false;
            attempts = MAX_ATTEMPTS;   /* A stall is retried slowly. */
            vTaskDelay(pdMS_TO_TICKS(RETRY_SLOW_MS));
        }
    }
}

/* ---- API --------------------------------------------------------------- */

void app_neck_hw_start(void)
{
    if (s_task) {
        return;
    }
    if (xTaskCreate(worker, "neck", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the neck task");
        s_task = NULL;
    }
}

void app_neck_hw_aim(float pan_deg, float tilt_deg)
{
    atomic_store(&s_aim_pan_cdeg, (int)(pan_deg * 100.0f));
    atomic_store(&s_aim_tilt_cdeg, (int)(tilt_deg * 100.0f));
}

void app_neck_hw_rest(bool rest)
{
    atomic_store(&s_rest, rest);
}

app_neck_state_t app_neck_hw_state(void)
{
    return (app_neck_state_t)atomic_load(&s_state);
}
