# app_game_runtime

Composes `app_game` with the Engine's native/Host module ABI: input translation,
fixed-step update, pixel-art drawing and animation. On the device only it also
persists the pet, polls the hardware pedometer and tracks the calendar day.

| File | Responsibility |
| --- | --- |
| `app_game_runtime.c` | Module ABI, input mapping, save, sensor and day polling |
| `pet_view.c` | Transitions, particles, floaters, card/food animations, chrome |
| `pet_screens.c` | Home (pet only), steps ring, feed, cards and slots content |
| `pet_bits.c` | Bit Flip: target, timer bar, eight switches, score |
| `pet_grid.c` | Spark Grid board, hints, PLAY/CASH and score |
| `pet_booth.c` | Photo booth: viewfinder, filters, countdown, flash, REC, review, gallery |
| `booth_media.c` | Booth pixels: live feed, captures, gallery cache, request handling |
| `pet_arcade.c` | Arcade lobby: cabinets, attract-mode demos, EXIT button |
| `pet_panels.c` | Pull-down menu, style picker, store, daily goal and Wi-Fi panels |
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
advance the model, serve the photo booth (feed, one request, status), take
its event bits, advance the view, then save if due.
`render` only reads a snapshot and draws; it never changes the model.

## Device behavior

- Steps: the BMI270 on-chip counter via `app_pedometer_hw`, so walking counts
  even while the pet is idle. Start is retried 6 times (every 5 s); if it
  never succeeds the model's software detector uses the board's IMU events.
- Save: NVS namespace `pixel_pal`, key `pet`, version 6 (v2..v5 records migrate
  by prefix, v2 with full health, before v6 with no photo looks or lenses; v1
  records are ignored since early builds could store an all-zero pet), the 76-byte
  `app_game_record_t`. Spending, settings and goals save after 2 s; step and stat
  drift after 30 s, to bound flash wear.
- Day: from `time()` once the clock is set (after network time sync); until
  then `steps_today` simply keeps accumulating.
- Screen power: dimming draws the scene at 30 % brightness and only every
  third frame (10 fps; the panel keeps the last frame in between); dozing
  draws one black frame and then submits nothing (no render, no transfer)
  until a tap.
  The panel itself is never commanded from the game loop: the board's frame
  task shares that bus and has no hook for out-of-band commands, and switching
  the panel off from here could hang the device. Black AMOLED pixels are off,
  so a black frame saves nearly as much.
- Sound: after each tick the runtime hands the events and snapshot to
  `app_sfx` (device only; see `components/app_sfx/README.md`).
- Battery: the BQ27220 gauge is read every 10 s; current above 5 mA counts as
  charging. A failed read reports "unknown" (menu shows NO BATT). For battery
  work the runtime keeps the mean discharge current per screen state since
  boot (awake / dim / off), logs it with the gauge's average current and
  time-to-empty every minute, and adds them to `/api/stats` (`power_ma`,
  `power_avg_ma`, `power_mah`, `power_full_mah`, `power_tte_min`, and
  `drain_ma` = [awake, dim, off], -1 before the first sample).
- Network: `app_net` starts 1 s after boot (first frame first). Every second
  the runtime reports Wi-Fi status/IP and the setup hotspot name to the model
  (shown in the menu and the Wi-Fi panel); `APP_EVENT_WIFI_SETUP/STOP` from the
  model start/stop the setup hotspot and are reported back on the same frame. It also
  publishes stats JSON for `http://pixelmate.local` / the menu's IP. A timezone
  from the web page is applied as a POSIX `TZ` and saved in NVS
  (`pixel_pal/tz_min`), then reloaded on boot.
- Day: the local calendar date as days since 1970 (consecutive across New
  Year). A date that moves backwards (clock or timezone correction) keeps
  today's progress; a forward jump is a new day.
- Photo booth: the runtime sets `app_camera_hw`'s mode every tick: STREAM while
  the owned Booth page is shown (not in its gallery), PROBE (slot EEPROM about
  once a second, camera released) while the Arcade, the Booth page or the store
  is on screen, OFF when the screen is off or elsewhere. Its debounced presence
  goes to the model with `app_game_set_camera()`, which adds or removes the
  Booth page and the booth's store items. Each new 128x128 feed frame is
  scaled to the selected lens and put through the selected look into
  `booth_media_t.live`, which the view draws to fill 256 px (8/4/2x for
  LO/MID/HI, one rectangle per run of equal shades). A PHOTO request makes a
  fresh frame at the lens and saves it with `app_photo_store`; GIF frames 0..7
  are made at the lens capped to MID and saved on frame 7; DELETE removes a
  gallery item. The
  result goes back with `app_game_booth_result()`, and the camera state and
  stored counts with `app_game_set_booth()` every tick. Saves run on the game
  loop: a GIF (8 KB NVS write) can stall one frame. Captures store the pet's
  color, so the gallery and web page keep the palette they were taken with.
- Board buttons never reset the pet. Only the Engine's explicit reset control does.

## Host simulator

The Host build compiles the files listed in `../../game.sim.json` without the
device-only code; the photo booth gets a synthetic feed (a face drifting over a
noisy gradient) and a RAM gallery, so every booth screen and look can be
checked without a camera. The pretend camera starts plugged in; the right key
(`right` action) unplugs and replugs it. It reports a fixed 76 % battery and draws black while the
screen is off. Wi-Fi starts as "nothing saved"; SET UP shows a stand-in
hotspot `PIXELMATE-SIM` and STOP returns to "nothing saved". The left key ends the simulated day (health check). The space key (`jump` action) credits 50 steps; replay `imu`
events drive the software pedometer. Source files include application public
headers by relative path because the Host runner cannot declare include paths.
