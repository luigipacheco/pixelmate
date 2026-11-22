// SPDX-License-Identifier: Apache-2.0
#pragma once
/* The StackChan's neck: two Feetech SCS0009 bus servos (pan and tilt) driven
 * by a worker task that shapes every move with a spring-damper filter. On
 * boards without a neck the same calls do nothing. Device-only; hardware
 * notes and limits in ../README.md. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_NECK_ABSENT = 0,   /**< No neck on this board, or start() not called. */
    APP_NECK_STARTING,     /**< Powering the servos and reading where the head is. */
    APP_NECK_READY,        /**< Holding and following app_neck_hw_aim(). */
    APP_NECK_RESTING,      /**< At home with torque released (app_neck_hw_rest). */
    APP_NECK_ERROR,        /**< Servos silent, out of the calibrated band or stalled;
                                torque is released and nothing moves. Retried slowly. */
} app_neck_state_t;

enum {
    APP_NECK_PAN_MAX_DEG = 40,    /**< Aim limit either side of home. */
    APP_NECK_TILT_MAX_DEG = 16,   /**< Aim limit above and below home. */
};

/** Start the neck worker. Idempotent; returns at once (bring-up takes ~1 s). */
void app_neck_hw_start(void);

/** Where the head should point, in degrees from home: pan positive turns the
 * head to its own left, tilt positive looks up. Clamped to the limits above.
 * Any task; the worker eases toward it. */
void app_neck_hw_aim(float pan_deg, float tilt_deg);

/** true: ease back home, then release the servos (screen asleep: quiet, no
 * holding current). false: take hold again from wherever the head is. */
void app_neck_hw_rest(bool rest);

app_neck_state_t app_neck_hw_state(void);

#ifdef __cplusplus
}
#endif
