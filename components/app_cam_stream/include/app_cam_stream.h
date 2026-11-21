// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Booth webcam server (port 81). Device-only; contracts in ../README.md. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Go on or off air with the viewer code shown on screen. Starts/stops the
 * server and the camera's JPEG encoding. Game loop only; cheap every tick. */
void app_cam_stream_set(bool on_air, int code);

#ifdef __cplusplus
}
#endif
