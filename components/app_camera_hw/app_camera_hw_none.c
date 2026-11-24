// SPDX-License-Identifier: Apache-2.0
/* Stand-in for boards whose camera is not ported yet (the Stack-chan's GC0308):
 * no camera is ever present, so the photo booth page and its store items stay
 * hidden. */
#include "app_camera_hw.h"

void app_camera_hw_set_mode(app_camera_mode_t mode)
{
    (void)mode;
}

bool app_camera_hw_present(void)
{
    return false;
}

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
    return APP_CAMERA_ABSENT;
}

bool app_camera_hw_frame(uint8_t yuv[APP_CAMERA_FRAME_BYTES], uint32_t *seq)
{
    (void)yuv;
    (void)seq;
    return false;
}
