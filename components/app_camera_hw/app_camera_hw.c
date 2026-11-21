// SPDX-License-Identifier: Apache-2.0
/* OV3640 DVP camera in the LEFT module slot, driven through esp_video without
 * the BSP's module manager: Raylib Lite Engine's board adapter already owns
 * I2C port 0 through Board Manager, and the BSP would create a second master
 * on that port. The slot/pin/pad sequence below is adapted from
 * esp-mosaico-bsp components/mosaico_module_camera/mosaico_camera.c and
 * components/esp-mosaico-bsp/onboard/subboard.c (Apache-2.0, Espressif). */
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include "app_camera_hw.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_cam_sensor_types.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#define BOARD_I2C "i2c_master"          /* Board Manager bus (v1.0 slots share it). */
#define HW_V1_0 0x0100U                 /* eFuse USER_DATA hardware revision. */
#define HW_V1_1 0x0101U
#define HW_V1_2 0x0102U
#define SLOT_I2C_PORT_V1_2 I2C_NUM_1    /* v1.2: dedicated sub-board bus on SDA 0 / SCL 1. */
#define SLOT_SDA GPIO_NUM_0
#define SLOT_SCL GPIO_NUM_1
#define LEFT_ADDR_SELECT GPIO_NUM_14    /* Standard modules: AT24C02 A0; camera: D4. */
#define FLASH_GPIO GPIO_NUM_34          /* Active-low flash LED gate; kept off. */
#define PWDN_GPIO GPIO_NUM_48
#define RESET_GPIO GPIO_NUM_53

enum {
    EEPROM_ADDR = 0x50,                 /* Camera boards fix their EEPROM here. */
    EEPROM_SPEED_HZ = 100000,
    EEPROM_TIMEOUT_MS = 50,
    BOARD_TYPE_CAMERA = 0x07,           /* mosaico_board_type_t MOSAICO_BOARD_TYPE_CAMERA */
    SCCB_SPEED_HZ = 100000,
    XCLK_SETTLE_MS = 20,                /* The board's own oscillator, before SCCB. */
    SENSOR_RESET_MS = 10,               /* RESET low pulse. */
    SENSOR_WAKE_MS = 30,                /* After PWDN low and RESET high, before SCCB. */
    PROBE_PERIOD_MS = 1000,             /* Slot EEPROM check while probing or streaming. */
    ABSENT_PROBES = 2,                  /* Misses in a row before "unplugged" (~1-2 s). */
    BUFFERS = 2,
    FRAME_TIMEOUT_MS = 1000,
    MAX_TIMEOUTS = 5,                   /* Consecutive frame timeouts before ERROR. */
    DISCARD_AFTER_START = 2,            /* Exposure settles on the first frames. */
    TASK_STACK = 4096,
    TASK_PRIORITY = 2,
    OV3640_PID_A = 0x364C,
    OV3640_PID_B = 0x3641,
    OV3640_REG_AWB_CTRL = 0x332B,
    OV3640_AWB_MANUAL_BIT = 0x08,
};

/* OV3640 advanced auto white balance, as applied by the BSP camera module. */
static const struct {
    uint16_t reg;
    uint8_t value;
} s_awb[] = {
    {0x3317, 0x04}, {0x3316, 0xF8}, {0x3312, 0x26}, {0x3314, 0x42},
    {0x3313, 0x2B}, {0x3315, 0x42}, {0x3310, 0xD0}, {0x3311, 0xBD},
    {0x330C, 0x18}, {0x330D, 0x18}, {0x330E, 0x56}, {0x330F, 0x5C},
    {0x330B, 0x1C}, {0x3306, 0x5C}, {0x3307, 0x11}, {0x3308, 0x25},
};

/* Shared between the worker and its callers. */
static _Atomic int s_state = APP_CAMERA_OFF;
static _Atomic int s_mode = APP_CAMERA_MODE_OFF;
static _Atomic bool s_present;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_frame_lock;
static EXT_RAM_BSS_ATTR uint8_t s_frame[APP_CAMERA_FRAME_BYTES];   /* Y, U, V planes, 48 KB: PSRAM. */
static uint32_t s_frame_seq;

/* Owned by the worker task only. */
static struct {
    i2c_master_bus_handle_t bus;
    bool bus_from_board;
    bool video_ready;
    int fd;
    void *buf[BUFFERS];
    size_t buf_len[BUFFERS];
    uint32_t width, height, bytes_per_line;
    bool streaming, tuned;
    bool slot_idle;                     /* Slot pins already parked for discovery. */
} s_cam = {.fd = -1};
static EXT_RAM_BSS_ATTR uint8_t s_luma[APP_CAMERA_FRAME_BYTES];   /* Worker's frame in progress. */

static void set_state(app_camera_state_t state)
{
    if (atomic_exchange(&s_state, (int)state) != (int)state) {
        ESP_LOGI(TAG, "state %d", (int)state);
    }
}

static esp_err_t xioctl(unsigned long request, void *arg, const char *what)
{
    if (ioctl(s_cam.fd, request, arg) == 0) {
        return ESP_OK;
    }
    /* esp_video maps "no frame yet" on DQBUF to EPERM (see the BSP module). */
    const bool timeout = errno == ETIMEDOUT || errno == EAGAIN || (request == VIDIOC_DQBUF && errno == EPERM);
    ESP_LOGW(TAG, "%s: errno %d", what, errno);
    return timeout ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

/* ---- Bring-up -------------------------------------------------------- */

static esp_err_t slot_bus(void)
{
    if (s_cam.bus) {
        return ESP_OK;
    }
    uint16_t revision = 0;
    ESP_RETURN_ON_ERROR(esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &revision, 16), TAG, "revision");
    if (revision == HW_V1_0) {
        /* v1.0: the slots hang off the mainboard bus, which Board Manager owns. */
        ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(BOARD_I2C, (void **)&s_cam.bus), TAG, "board i2c");
        s_cam.bus_from_board = true;
        ESP_LOGI(TAG, "board v1.0: slot bus = board I2C0");
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(revision == HW_V1_1 || revision == HW_V1_2, ESP_ERR_NOT_SUPPORTED, TAG,
                        "unknown board revision 0x%04x", revision);
    const i2c_master_bus_config_t config = {
        .i2c_port = SLOT_I2C_PORT_V1_2,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = SLOT_SDA,
        .scl_io_num = SLOT_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &s_cam.bus), TAG, "slot i2c");
    ESP_LOGI(TAG, "board v1.%u: slot bus = I2C1", revision & 0xFFU);
    return ESP_OK;
}

static esp_err_t output(gpio_num_t pin, int level, gpio_mode_t mode)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(pin), .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level), TAG, "preset gpio %d", pin);
    return gpio_config(&config);
}

/* Is a camera board in the left slot? Reads the EEPROM descriptor's magic and
 * board type (the BSP module manager's claim, minus its CRC bookkeeping). */
static esp_err_t probe_slot(void)
{
    if (!s_cam.slot_idle) {   /* Parked pins already drive the address select low. */
        ESP_RETURN_ON_ERROR(output(LEFT_ADDR_SELECT, 0, GPIO_MODE_OUTPUT), TAG, "address select");
    }
    if (i2c_master_probe(s_cam.bus, EEPROM_ADDR, EEPROM_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = EEPROM_ADDR, .scl_speed_hz = EEPROM_SPEED_HZ,
    };
    i2c_master_dev_handle_t eeprom = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_cam.bus, &config, &eeprom), TAG, "eeprom");
    const uint8_t offset = 0;
    uint8_t head[4] = {0};
    const esp_err_t err = i2c_master_transmit_receive(eeprom, &offset, 1, head, sizeof(head), EEPROM_TIMEOUT_MS);
    i2c_master_bus_rm_device(eeprom);
    ESP_RETURN_ON_ERROR(err, TAG, "eeprom read");
    if (memcmp(head, "ESP", 3) != 0 || head[3] != BOARD_TYPE_CAMERA) {
        ESP_LOGW(TAG, "left slot holds board type 0x%02x, not a camera", memcmp(head, "ESP", 3) ? 0 : head[3]);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

/* Camera D2 is GPIO33, the USB-Serial/JTAG pad; the console is on UART and the
 * PC link is ESP-Iris on High-Speed USB, so the pad is simply switched off. */
static void release_usj_pad(void)
{
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_serial_jtag_ll_enable_bus_clock(false);
}

static esp_err_t init_video(void)
{
    if (s_cam.video_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(output(FLASH_GPIO, 1, GPIO_MODE_OUTPUT_OD), TAG, "flash off");
    release_usj_pad();
    /* Wake the sensor before esp_cam_sensor probes it: a released camera is
     * parked in power-down, and the driver only allows 10 ms after lowering
     * PWDN. Leave power-down, pulse RESET, then let the module settle. */
    ESP_RETURN_ON_ERROR(output(PWDN_GPIO, 0, GPIO_MODE_OUTPUT), TAG, "pwdn");
    ESP_RETURN_ON_ERROR(output(RESET_GPIO, 0, GPIO_MODE_OUTPUT), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(SENSOR_RESET_MS));
    gpio_set_level(RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(XCLK_SETTLE_MS + SENSOR_WAKE_MS));
    esp_video_init_dvp_config_t dvp = {
        .sccb_config = {.init_sccb = false, .i2c_handle = s_cam.bus, .freq = SCCB_SPEED_HZ},
        .reset_pin = RESET_GPIO,
        .pwdn_pin = PWDN_GPIO,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {GPIO_NUM_16, GPIO_NUM_15, GPIO_NUM_33, GPIO_NUM_4,
                        LEFT_ADDR_SELECT, GPIO_NUM_12, GPIO_NUM_18, GPIO_NUM_13},
            .vsync_io = GPIO_NUM_55,
            .de_io = GPIO_NUM_19,
            .pclk_io = GPIO_NUM_17,
            .xclk_io = GPIO_NUM_NC,
        },
        .xclk_freq = 0,
    };
    const esp_video_init_config_t config = {.dvp = &dvp};
    ESP_RETURN_ON_ERROR(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP), TAG, "esp_video_init");
    s_cam.video_ready = true;
    return ESP_OK;
}

static esp_err_t write_reg(uint16_t reg, uint8_t value)
{
    esp_cam_sensor_reg_val_t rv = {.regaddr = reg, .value = value};
    struct v4l2_ext_control control = {.id = ESP_CAM_SENSOR_IOC_S_REG, .size = sizeof(rv), .p_u8 = (uint8_t *)&rv};
    struct v4l2_ext_controls controls = {.ctrl_class = V4L2_CTRL_CLASS_ESP_CAM_IOCTL, .count = 1, .controls = &control};
    return xioctl(VIDIOC_S_EXT_CTRLS, &controls, "set reg");
}

static esp_err_t read_reg(uint16_t reg, uint8_t *value)
{
    esp_cam_sensor_reg_val_t rv = {.regaddr = reg};
    struct v4l2_ext_control control = {.id = ESP_CAM_SENSOR_IOC_G_REG, .size = sizeof(rv), .p_u8 = (uint8_t *)&rv};
    struct v4l2_ext_controls controls = {.ctrl_class = V4L2_CTRL_CLASS_ESP_CAM_IOCTL, .count = 1, .controls = &control};
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_G_EXT_CTRLS, &controls, "get reg"), TAG, "reg %04x", reg);
    *value = (uint8_t)rv.value;
    return ESP_OK;
}

static void tune_ov3640(void)
{
    for (size_t i = 0; i < sizeof(s_awb) / sizeof(s_awb[0]); ++i) {
        if (write_reg(s_awb[i].reg, s_awb[i].value) != ESP_OK) {
            return;
        }
    }
    uint8_t awb = 0;
    if (read_reg(OV3640_REG_AWB_CTRL, &awb) == ESP_OK) {
        write_reg(OV3640_REG_AWB_CTRL, (uint8_t)(awb & ~OV3640_AWB_MANUAL_BIT));
    }
}

static void close_device(void)
{
    for (int i = 0; i < BUFFERS; ++i) {
        if (s_cam.buf[i] && s_cam.buf[i] != MAP_FAILED) {
            munmap(s_cam.buf[i], s_cam.buf_len[i]);
        }
        s_cam.buf[i] = NULL;
    }
    if (s_cam.fd >= 0) {
        struct v4l2_requestbuffers none = {.count = 0, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
        ioctl(s_cam.fd, VIDIOC_REQBUFS, &none);
        close(s_cam.fd);
        s_cam.fd = -1;
    }
}

static esp_err_t open_device_once(void)
{
    s_cam.fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    ESP_RETURN_ON_FALSE(s_cam.fd >= 0, ESP_ERR_NOT_FOUND, TAG, "open %s: %d", ESP_VIDEO_DVP_DEVICE_NAME, errno);
    esp_cam_sensor_id_t id = {0};
    struct v4l2_ext_control control = {.id = ESP_CAM_SENSOR_IOC_G_CHIP_ID, .size = sizeof(id), .p_u8 = (uint8_t *)&id};
    struct v4l2_ext_controls controls = {.ctrl_class = V4L2_CTRL_CLASS_ESP_CAM_IOCTL, .count = 1, .controls = &control};
    if (xioctl(VIDIOC_G_EXT_CTRLS, &controls, "chip id") == ESP_OK) {
        ESP_LOGI(TAG, "sensor pid 0x%04x", id.pid);
        s_cam.tuned = !(id.pid == OV3640_PID_A || id.pid == OV3640_PID_B);   /* Tune only an OV3640. */
    }
    struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_G_FMT, &format, "G_FMT"), TAG, "format");
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_S_FMT, &format, "S_FMT"), TAG, "format");
    ESP_RETURN_ON_FALSE(format.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY && format.fmt.pix.width >= 4 * APP_PHOTO_MAX_SIDE
                        && format.fmt.pix.height >= 4 * APP_PHOTO_MAX_SIDE, ESP_ERR_NOT_SUPPORTED, TAG, "format");
    s_cam.width = format.fmt.pix.width;
    s_cam.height = format.fmt.pix.height;
    s_cam.bytes_per_line = format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : s_cam.width * 2U;
    struct timeval timeout = {.tv_sec = FRAME_TIMEOUT_MS / 1000, .tv_usec = (FRAME_TIMEOUT_MS % 1000) * 1000};
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_S_DQBUF_TIMEOUT, &timeout, "timeout"), TAG, "timeout");
    struct v4l2_requestbuffers request = {.count = BUFFERS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
               .memory = V4L2_MEMORY_MMAP
    };
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_REQBUFS, &request, "REQBUFS"), TAG, "buffers");
    ESP_RETURN_ON_FALSE(request.count == BUFFERS, ESP_ERR_NO_MEM, TAG, "got %u buffers", (unsigned)request.count);
    for (uint32_t i = 0; i < BUFFERS; ++i) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        ESP_RETURN_ON_ERROR(xioctl(VIDIOC_QUERYBUF, &buffer, "QUERYBUF"), TAG, "buffer");
        s_cam.buf[i] = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_cam.fd, buffer.m.offset);
        ESP_RETURN_ON_FALSE(s_cam.buf[i] != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "mmap");
        s_cam.buf_len[i] = buffer.length;
    }
    ESP_LOGI(TAG, "opened %lux%lu UYVY", (unsigned long)s_cam.width, (unsigned long)s_cam.height);
    return ESP_OK;
}

static esp_err_t open_device(void)
{
    if (s_cam.fd >= 0) {
        return ESP_OK;
    }
    const esp_err_t err = open_device_once();
    if (err != ESP_OK) {
        close_device();   /* A later retry starts from a clean device. */
    }
    return err;
}

/* ---- Streaming -------------------------------------------------------- */

static esp_err_t queue_all(void)
{
    for (uint32_t i = 0; i < BUFFERS; ++i) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        ESP_RETURN_ON_ERROR(xioctl(VIDIOC_QBUF, &buffer, "QBUF"), TAG, "queue");
    }
    return ESP_OK;
}

static esp_err_t start_stream(void)
{
    ESP_RETURN_ON_ERROR(queue_all(), TAG, "queue");
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_STREAMON, &type, "STREAMON"), TAG, "stream on");
    s_cam.streaming = true;
    if (!s_cam.tuned) {
        tune_ov3640();
        s_cam.tuned = true;
    }
    return ESP_OK;
}

/* Center-crop the frame to a square and average 2x2 luminance samples per
 * output pixel (128x128: a 6x6 source block each from 768x768). The sensor is mounted sideways: like the BSP example's PPA
 * setup (rotate 270 CCW, then mirror y), out(x, y) = in(S-1-y, S-1-x). */
static void downsample(const uint8_t *uyvy)
{
    /* Largest centered square that is a whole number of blocks: 768 of
     * 1024x768 (OV3640, 6 px blocks), 640 of 1280x720 (SC101IOT, 5 px). */
    const uint32_t short_side = s_cam.width < s_cam.height ? s_cam.width : s_cam.height;
    const uint32_t block = short_side / APP_PHOTO_MAX_SIDE;
    const uint32_t side = block * APP_PHOTO_MAX_SIDE;
    const uint32_t x0 = (s_cam.width - side) / 2, y0 = (s_cam.height - side) / 2;
    const uint32_t a = block / 4, b = block * 3 / 4;
    for (uint32_t by = 0; by < APP_PHOTO_MAX_SIDE; ++by) {
        const uint8_t *row_a = uyvy + (size_t)(y0 + by * block + a) * s_cam.bytes_per_line;
        const uint8_t *row_b = uyvy + (size_t)(y0 + by * block + b) * s_cam.bytes_per_line;
        for (uint32_t bx = 0; bx < APP_PHOTO_MAX_SIDE; ++bx) {
            const size_t xa = (size_t)(x0 + bx * block + a) * 2U + 1U, xb = (size_t)(x0 + bx * block + b) * 2U + 1U;
            const uint32_t sum = row_a[xa] + row_a[xb] + row_b[xa] + row_b[xb];
            /* Chroma: the U/V pair of the macropixel holding sample a, both rows. */
            const size_t mc = ((size_t)(x0 + bx * block + a) & ~(size_t)1U) * 2U;
            const uint32_t u = row_a[mc] + row_b[mc], v = row_a[mc + 2] + row_b[mc + 2];
            const uint32_t ox = APP_PHOTO_MAX_SIDE - 1U - by, oy = APP_PHOTO_MAX_SIDE - 1U - bx;
            const size_t o = oy * APP_PHOTO_MAX_SIDE + ox;
            s_luma[o] = (uint8_t)(sum / 4U);
            s_luma[APP_PHOTO_MAX_PIXELS + o] = (uint8_t)(u / 2U);
            s_luma[2 * APP_PHOTO_MAX_PIXELS + o] = (uint8_t)(v / 2U);
        }
    }
}

/* Every dequeued buffer is requeued before this returns, so a later STREAMOFF
 * or close never finds a frame still borrowed. */
static esp_err_t capture(void)
{
    struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_DQBUF, &buffer, "DQBUF"), TAG, "frame");
    const bool ok = buffer.index < BUFFERS && !(buffer.flags & V4L2_BUF_FLAG_ERROR)
                    && buffer.bytesused >= (size_t)s_cam.bytes_per_line * s_cam.height;
    if (ok) {
        downsample(s_cam.buf[buffer.index]);
    }
    const esp_err_t requeue = xioctl(VIDIOC_QBUF, &buffer, "QBUF");
    if (!ok) {
        return requeue == ESP_OK ? ESP_ERR_INVALID_SIZE : requeue;
    }
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    memcpy(s_frame, s_luma, sizeof(s_frame));
    ++s_frame_seq;
    xSemaphoreGive(s_frame_lock);
    return requeue;
}

/* Stop, close and deinitialize the camera, then give the slot back to
 * discovery: GPIO14 address select low, the sensor held in power-down, the
 * flash off. Safe at any time, also after the board was pulled out. */
static void release_camera(void)
{
    if (s_cam.slot_idle) {
        return;   /* Already released: nothing to stop, pins already parked. */
    }
    if (s_cam.streaming) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(VIDIOC_STREAMOFF, &type, "STREAMOFF");
        s_cam.streaming = false;
    }
    close_device();
    if (s_cam.video_ready) {
        const esp_err_t err = esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_video deinit: %s", esp_err_to_name(err));
        }
        s_cam.video_ready = false;
        s_cam.tuned = false;
        ESP_LOGI(TAG, "camera released");
    }
    output(LEFT_ADDR_SELECT, 0, GPIO_MODE_OUTPUT);
    output(PWDN_GPIO, 1, GPIO_MODE_OUTPUT);
    output(FLASH_GPIO, 1, GPIO_MODE_OUTPUT_OD);
    s_cam.slot_idle = true;
}

/* Diagnostics after a failed start: which 7-bit addresses answer on a bus. */
static void log_bus_scan(i2c_master_bus_handle_t bus, const char *name)
{
    char found[96];
    int n = 0;
    found[0] = 0;
    for (uint8_t addr = 0x08; bus && addr < 0x78 && n < (int)sizeof(found) - 6; ++addr) {
        if (i2c_master_probe(bus, addr, EEPROM_TIMEOUT_MS) == ESP_OK) {
            n += snprintf(found + n, sizeof(found) - (size_t)n, " 0x%02x", addr);
        }
    }
    ESP_LOGW(TAG, "%s bus answers:%s", name, n ? found : " nothing");
}

static void diagnose_failure(void)
{
    log_bus_scan(s_cam.bus, "slot");
    i2c_master_bus_handle_t board = NULL;
    if (!s_cam.bus_from_board && esp_board_periph_ref_handle(BOARD_I2C, (void **)&board) == ESP_OK) {
        log_bus_scan(board, "board");
        esp_board_periph_unref_handle(BOARD_I2C);
    }
}

static esp_err_t bring_up(void)
{
    s_cam.slot_idle = false;   /* Any failure below must be released again. */
    if (!s_cam.video_ready) {
        ESP_RETURN_ON_ERROR(init_video(), TAG, "video");
    }
    ESP_RETURN_ON_ERROR(open_device(), TAG, "open");
    return start_stream();
}

/* One presence check. While absent the EEPROM descriptor is read to be sure
 * it is a camera; while present a bare address probe is enough. "Unplugged"
 * needs ABSENT_PROBES misses in a row, so one NACK on a shared bus does not
 * tear the camera down. */
static void probe_presence(void)
{
    static uint8_t misses;   /* Worker task only. */
    if (slot_bus() != ESP_OK) {
        atomic_store(&s_present, false);
        return;
    }
    const bool present = atomic_load(&s_present);
    const bool hit = present ? i2c_master_probe(s_cam.bus, EEPROM_ADDR, EEPROM_TIMEOUT_MS) == ESP_OK
                     : probe_slot() == ESP_OK;
    if (hit) {
        misses = 0;
        if (!present) {
            ESP_LOGI(TAG, "camera plugged in");
            atomic_store(&s_present, true);
        }
    } else if (present && ++misses >= ABSENT_PROBES) {
        misses = 0;
        ESP_LOGI(TAG, "camera unplugged");
        atomic_store(&s_present, false);
    }
}

static void worker(void *arg)
{
    (void)arg;
    int timeouts = 0, discard = 0;
    TickType_t last_probe = 0;
    bool probed = false, was_present = false;
    app_camera_mode_t last_mode = APP_CAMERA_MODE_OFF;
    for (;;) {
        const app_camera_mode_t mode = (app_camera_mode_t)atomic_load(&s_mode);
        if (mode != last_mode) {
            probed = false;                       /* Probe at once after every change. */
            if (atomic_load(&s_state) == APP_CAMERA_ERROR) {
                set_state(APP_CAMERA_OFF);        /* Reopening the booth retries. */
            }
            last_mode = mode;
        }
        if (mode == APP_CAMERA_MODE_OFF) {
            release_camera();
            set_state(APP_CAMERA_OFF);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        const TickType_t now = xTaskGetTickCount();
        if (!probed || now - last_probe >= pdMS_TO_TICKS(PROBE_PERIOD_MS)) {
            probe_presence();
            last_probe = now;
            probed = true;
        }
        const bool present = atomic_load(&s_present);
        if (present && !was_present && atomic_load(&s_state) == APP_CAMERA_ERROR) {
            set_state(APP_CAMERA_OFF);            /* A replug clears an old error. */
        }
        was_present = present;
        const bool failed = atomic_load(&s_state) == APP_CAMERA_ERROR;
        if (!present || mode == APP_CAMERA_MODE_PROBE || failed) {
            release_camera();
            if (!present) {
                set_state(APP_CAMERA_ABSENT);
            } else if (!failed) {
                set_state(APP_CAMERA_OFF);
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PROBE_PERIOD_MS));
            continue;
        }
        if (!s_cam.streaming) {
            set_state(APP_CAMERA_STARTING);
            const esp_err_t err = bring_up();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "start: %s", esp_err_to_name(err));
                release_camera();
                diagnose_failure();
                set_state(APP_CAMERA_ERROR);
                continue;
            }
            timeouts = 0;
            discard = DISCARD_AFTER_START;
        }
        const esp_err_t err = capture();
        if (err == ESP_OK) {
            timeouts = 0;
            if (discard > 0) {
                --discard;
            } else {
                set_state(APP_CAMERA_LIVE);
            }
        } else if (++timeouts >= MAX_TIMEOUTS) {
            ESP_LOGW(TAG, "no frames (%s); stopping", esp_err_to_name(err));
            release_camera();
            set_state(APP_CAMERA_ERROR);
        }
    }
}

/* ---- API ------------------------------------------------------------- */

void app_camera_hw_set_mode(app_camera_mode_t mode)
{
    if (atomic_exchange(&s_mode, (int)mode) == (int)mode) {
        return;
    }
    if (!s_task && mode != APP_CAMERA_MODE_OFF) {
        s_frame_lock = xSemaphoreCreateMutex();
        if (!s_frame_lock || xTaskCreate(worker, "camera", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
            ESP_LOGE(TAG, "no memory for the camera task");
            set_state(APP_CAMERA_ERROR);
            s_task = NULL;
            atomic_store(&s_mode, APP_CAMERA_MODE_OFF);
            return;
        }
    }
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

bool app_camera_hw_present(void)
{
    return atomic_load(&s_present);
}

app_camera_state_t app_camera_hw_state(void)
{
    return (app_camera_state_t)atomic_load(&s_state);
}

bool app_camera_hw_frame(uint8_t yuv[APP_CAMERA_FRAME_BYTES], uint32_t *seq)
{
    if (!s_frame_lock || !yuv || !seq) {
        return false;
    }
    bool copied = false;
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    if (s_frame_seq && s_frame_seq != *seq) {
        memcpy(yuv, s_frame, sizeof(s_frame));
        *seq = s_frame_seq;
        copied = true;
    }
    xSemaphoreGive(s_frame_lock);
    return copied;
}
