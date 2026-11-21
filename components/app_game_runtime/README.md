# app_game_runtime

Composes `app_game` with the Engine's native/Host module ABI: input translation,
fixed-step update, pixel-art drawing and animation. On the device only it also
persists the pet, polls the hardware pedometer and tracks the calendar day.

| File | Responsibility |
| --- | --- |
| `app_game_runtime.c` | Module ABI, input mapping, save, sensor and day polling |
| `pet_view.c` | Transitions, particles, floaters, card/food animations, chrome |
| `pet_screens.c` | Home (pet only), steps ring, feed, cards and slots content |
| `pet_panels.c` | Pull-down menu, style picker, store and daily goal panels |
| `pet_eyes.c` | Eye shapes per mood with spring physics, blinks and gaze |
| `pixel_draw.c` | Grid rasterizer, 5x7 font, icons, palette |

```c
const raylib_lite_game_module_v1_t *module = app_game_runtime_module();
/* The Engine allocates module->state_size and drives the documented lifecycle. */
```

## Lifecycle

The Engine allocates zeroed `state_size` bytes per instance and calls the
callbacks serially on one loop, never from an ISR. The state owns the model
handle, the view (all animation state, instance-owned, no heap) and, on the
device, the save slot. `initialize` returns -1 only when the model cannot be
allocated; a missing NVS or IMU degrades to "no persistence" / "software steps".
`shutdown` forces a final save, then releases the model.

`update` order per 30 Hz tick: poll hardware steps (1 s) and the day (10 s),
advance the model, take its event bits, advance the view, then save if due.
`render` only reads a snapshot and draws; it never changes the model.

## Device behavior

- Steps: the BMI270 on-chip counter via `app_pedometer_hw`, so walking counts
  even while the pet is idle. Start is retried 6 times (every 5 s); if it
  never succeeds the model's software detector uses the board's IMU events.
- Save: NVS namespace `pixel_pal`, key `pet`, version 3 (v2 records migrate by
  prefix with full health; v1 records are ignored since early builds could
  store an all-zero pet), the 36-byte
  `app_game_record_t`. Spending, settings and goals save after 2 s; step and stat
  drift after 30 s, to bound flash wear.
- Day: from `time()` once the clock is set (after network time sync); until
  then `steps_today` simply keeps accumulating.
- Screen power: `app_power_hw` mirrors the model's power state onto the panel
  (awake 80 % brightness, dim 12 %, doze = panel off). While the panel is off,
  `render` submits nothing, skipping drawing and the QSPI transfer; updates
  (steps, stats) keep running at 30 Hz. Touch stays active to wake it.
- Battery: the BQ27220 gauge is read every 10 s; current above 5 mA counts as
  charging. A failed read reports "unknown" (menu shows NO BATT).
- Board buttons never reset the pet. Only the Engine's explicit reset control does.

## Host simulator

The Host build compiles the files listed in `../../game.sim.json` without the
device-only code; it reports a fixed 76 % battery and draws black while the
screen is off. The left key ends the simulated day (health check). The space key (`jump` action) credits 50 steps; replay `imu`
events drive the software pedometer. Source files include application public
headers by relative path because the Host runner cannot declare include paths.
