// SPDX-License-Identifier: Apache-2.0
/* Boards without face detection (the ESP-Mosaico): never sees a face. */
#include "app_face_hw.h"

void app_face_hw_enable(bool on)
{
    (void)on;
}

bool app_face_hw_take(bool *seen, float *x, float *y)
{
    (void)seen;
    (void)x;
    (void)y;
    return false;
}
