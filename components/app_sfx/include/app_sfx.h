// SPDX-License-Identifier: Apache-2.0
#pragma once
/* 8-bit sound effects for the pet (native builds only; the Host simulator does
 * not link audio). Turns the model's one-shot APP_EVENT_* bits into short
 * clips packed from assets_src/. Owned by the game loop: no locking. */
#include <stdint.h>
#include "app_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Call once per game tick, after app_game_take_events(), with that tick's
 * events and snapshot. The first call opens the codec and loads the clips;
 * it plays the boot chime instead of that tick's events. Honors
 * snapshot->sound (OFF / LOW / MID / HIGH) and stays silent while the screen is off. */
void app_sfx_update(uint32_t events, const app_game_snapshot_t *snapshot);

/** Stop playback, unload the clips and release the codec. Safe to repeat. */
void app_sfx_close(void);

#ifdef __cplusplus
}
#endif
