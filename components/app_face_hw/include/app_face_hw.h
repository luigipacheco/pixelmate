// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Finds a face in the camera feed (app_camera_hw's 128x128 frames) so the pet
 * can look at whoever is in front of it. Runs on the device only: nothing is
 * stored or sent. Boards without the detector get calls that do nothing.
 * Device-only; call from the game loop task. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Run the detector (true) or pause it (false). The first enable starts a
 * low-priority worker; it only works while the camera is streaming. */
void app_face_hw_enable(bool on);

/** Result of a detection run that finished since the last call. Returns false
 * when no new run finished. Otherwise *seen says whether a face was found, and
 * x, y give the center of the largest one in the frame: -1..1, +x toward the
 * image's right, +y down. */
bool app_face_hw_take(bool *seen, float *x, float *y);

#ifdef __cplusplus
}
#endif
