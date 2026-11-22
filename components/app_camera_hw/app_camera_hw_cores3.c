// SPDX-License-Identifier: Apache-2.0
/* GC0308 camera built into the M5Stack CoreS3 (the StackChan's head), reduced
 * to the same 128x128 Y/U/V feed the photo booth gets on the ESP-Mosaico.
 *
 * ESP Board Manager's "camera" device starts esp_video for the DVP sensor on
 * the board's shared I2C bus (touch, IMU and the power chip live there too, so
 * the bus is never taken over). The sensor is always there: "present" is true
 * from the first call and a bring-up failure shows as APP_CAMERA_ERROR.
 *
 * A worker task owns the camera; the game loop only picks a mode and copies
 * frames, as with the Mosaico driver (app_camera_hw.c). */
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include "app_camera_hw.h"
#include "esp_attr.h"
#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#define CAMERA_DEVICE "camera"          /* Board Manager device name. */

enum {
    BUFFERS = 2,
    FRAME_TIMEOUT_MS = 1000,
    MAX_TIMEOUTS = 5,                   /* Consecutive frame timeouts before ERROR. */
    DISCARD_AFTER_START = 3,            /* Exposure settles on the first frames. */
    TASK_STACK = 4096,
    TASK_PRIORITY = 2,
    SIDE = APP_PHOTO_MAX_SIDE,          /* Output is SIDE x SIDE. */
};

/* Shared between the worker and its callers. */
static _Atomic int s_state = APP_CAMERA_OFF;
static _Atomic int s_mode = APP_CAMERA_MODE_OFF;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_frame_lock;
static EXT_RAM_BSS_ATTR uint8_t s_frame[APP_CAMERA_FRAME_BYTES];   /* Y, U, V planes, 48 KB: PSRAM. */
static uint32_t s_frame_seq;

/* Owned by the worker task only. */
static struct {
    bool video_ready;                   /* Board Manager camera device initialized. */
    int fd;
    void *buf[BUFFERS];
    size_t buf_len[BUFFERS];
    uint32_t width, height, bytes_per_line;
    uint8_t y_off, u_off, v_off;        /* Byte offsets inside a 4-byte macropixel. */
    bool streaming;
} s_cam = {.fd = -1};
static EXT_RAM_BSS_ATTR uint8_t s_work[APP_CAMERA_FRAME_BYTES];    /* Worker's frame in progress. */

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
    /* esp_video maps "no frame yet" on DQBUF to EPERM. */
    const bool timeout = errno == ETIMEDOUT || errno == EAGAIN || (request == VIDIOC_DQBUF && errno == EPERM);
    ESP_LOGW(TAG, "%s: errno %d", what, errno);
    return timeout ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

/* ---- Bring-up -------------------------------------------------------- */

static void close_device(void)
{
    for (uint32_t i = 0; i < BUFFERS; ++i) {
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
    /* The sensor's default format is chosen in sdkconfig (YUV422 YUYV,
     * 320x240: the 640x480 mode doubled the picture). Either byte order
     * works; anything else is refused. */
    struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_G_FMT, &format, "G_FMT"), TAG, "format");
    const uint32_t pixel = format.fmt.pix.pixelformat;
    ESP_RETURN_ON_FALSE(pixel == V4L2_PIX_FMT_YUYV || pixel == V4L2_PIX_FMT_UYVY, ESP_ERR_NOT_SUPPORTED, TAG,
                        "pixel format 0x%08lx", (unsigned long)pixel);
    ESP_RETURN_ON_FALSE(format.fmt.pix.width >= SIDE && format.fmt.pix.height >= SIDE, ESP_ERR_NOT_SUPPORTED, TAG,
                        "frame too small");
    s_cam.width = format.fmt.pix.width;
    s_cam.height = format.fmt.pix.height;
    s_cam.bytes_per_line = format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : s_cam.width * 2U;
    const bool yuyv = pixel == V4L2_PIX_FMT_YUYV;   /* Y0 U Y1 V, else U Y0 V Y1. */
    s_cam.y_off = yuyv ? 0 : 1;
    s_cam.u_off = yuyv ? 1 : 0;
    s_cam.v_off = yuyv ? 3 : 2;
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
    ESP_LOGI(TAG, "opened %lux%lu %s", (unsigned long)s_cam.width, (unsigned long)s_cam.height, yuyv ? "YUYV" : "UYVY");
    return ESP_OK;
}

static esp_err_t bring_up(void)
{
    if (!s_cam.video_ready) {
        /* The board profile keeps the camera off at boot (init_skip): this
         * powers it and starts esp_video on the shared I2C bus. */
        ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(CAMERA_DEVICE), TAG, "camera device");
        s_cam.video_ready = true;
    }
    if (s_cam.fd < 0) {
        const esp_err_t err = open_device_once();
        if (err != ESP_OK) {
            close_device();   /* A later retry starts from a clean device. */
            return err;
        }
    }
    for (uint32_t i = 0; i < BUFFERS; ++i) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        ESP_RETURN_ON_ERROR(xioctl(VIDIOC_QBUF, &buffer, "QBUF"), TAG, "queue");
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(xioctl(VIDIOC_STREAMON, &type, "STREAMON"), TAG, "stream on");
    s_cam.streaming = true;
    return ESP_OK;
}

/* Stop streaming and close the device. esp_video stays initialized: the
 * sensor idles and the next start is quick. */
static void release_camera(void)
{
    if (s_cam.streaming) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(VIDIOC_STREAMOFF, &type, "STREAMOFF");
        s_cam.streaming = false;
    }
    close_device();
}

/* ---- Frames ---------------------------------------------------------- */

/* Center-crop the frame to a square (240 of 320x240) and reduce it to
 * SIDE x SIDE. Each output pixel averages four luminance samples spread over
 * its source cell (1.875 pixels at 320x240); chroma comes from the cell's first
 * macropixel on two rows. The sensor's default orientation is already upright
 * on the CoreS3. */
static void downsample(const uint8_t *yuv)
{
    const uint32_t side = s_cam.width < s_cam.height ? s_cam.width : s_cam.height;
    const uint32_t x0 = (s_cam.width - side) / 2, y0 = (s_cam.height - side) / 2;
    const uint32_t reach = side / SIDE >= 3 ? 2 : (side / SIDE >= 2 ? 1 : 0);   /* Second sample offset. */
    for (uint32_t oy = 0; oy < SIDE; ++oy) {
        const uint32_t sy = y0 + oy * side / SIDE;
        const uint8_t *row_a = yuv + (size_t)sy * s_cam.bytes_per_line;
        const uint8_t *row_b = yuv + (size_t)(sy + reach) * s_cam.bytes_per_line;
        for (uint32_t ox = 0; ox < SIDE; ++ox) {
            const uint32_t sx = x0 + ox * side / SIDE;
            const size_t ya = (size_t)sx * 2U + s_cam.y_off, yb = (size_t)(sx + reach) * 2U + s_cam.y_off;
            const uint32_t sum = row_a[ya] + row_a[yb] + row_b[ya] + row_b[yb];
            const size_t mc = ((size_t)sx & ~(size_t)1U) * 2U;   /* Macropixel holding sample a. */
            const uint32_t u = row_a[mc + s_cam.u_off] + row_b[mc + s_cam.u_off];
            const uint32_t v = row_a[mc + s_cam.v_off] + row_b[mc + s_cam.v_off];
            const size_t o = (size_t)oy * SIDE + ox;
            s_work[o] = (uint8_t)(sum / 4U);
            s_work[APP_PHOTO_MAX_PIXELS + o] = (uint8_t)(u / 2U);
            s_work[2 * APP_PHOTO_MAX_PIXELS + o] = (uint8_t)(v / 2U);
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
    memcpy(s_frame, s_work, sizeof(s_frame));
    ++s_frame_seq;
    xSemaphoreGive(s_frame_lock);
    return requeue;
}

static void worker(void *arg)
{
    (void)arg;
    int timeouts = 0, discard = 0;
    app_camera_mode_t last_mode = APP_CAMERA_MODE_OFF;
    for (;;) {
        const app_camera_mode_t mode = (app_camera_mode_t)atomic_load(&s_mode);
        if (mode != last_mode) {
            if (atomic_load(&s_state) == APP_CAMERA_ERROR) {
                set_state(APP_CAMERA_OFF);        /* Reopening the booth retries. */
            }
            last_mode = mode;
        }
        const bool failed = atomic_load(&s_state) == APP_CAMERA_ERROR;
        if (mode != APP_CAMERA_MODE_STREAM || failed) {
            release_camera();
            if (!failed) {
                set_state(APP_CAMERA_OFF);
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* Until the mode changes. */
            continue;
        }
        if (!s_cam.streaming) {
            set_state(APP_CAMERA_STARTING);
            const esp_err_t err = bring_up();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "start: %s", esp_err_to_name(err));
                release_camera();
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
        /* CPU 0: the game loop draws on CPU 1, and each frame's reduction is
         * real work at 16 frames a second. */
        if (!s_frame_lock
                || xTaskCreatePinnedToCore(worker, "camera", TASK_STACK, NULL, TASK_PRIORITY, &s_task, 0) != pdPASS) {
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
    return true;   /* Built in. */
}

/* Webcam: this chip has no hardware JPEG encoder; not ported yet. */
void app_camera_hw_set_webcam(bool on)
{
    (void)on;
}

size_t app_camera_hw_jpeg(uint8_t *out, size_t cap, uint32_t *seq)
{
    (void)out;
    (void)cap;
    (void)seq;
    return 0;
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
    if (s_frame_seq != *seq) {
        memcpy(yuv, s_frame, APP_CAMERA_FRAME_BYTES);
        *seq = s_frame_seq;
        copied = true;
    }
    xSemaphoreGive(s_frame_lock);
    return copied;
}
