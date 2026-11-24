# app_game

The pet's rules in pure C, with no Engine, RTOS, display or hardware dependency:
step counting, sparks, hunger/happiness/friendship, moods, screen navigation,
touch gestures, feeding and the high-low card game.

| File | Responsibility |
| --- | --- |
| `app_game.c` | Pet state, moods, gestures, screens, save record |
| `pedometer.c` | Software step detector over accelerometer magnitude |
| `cards.c` | High-low rules (deal, guess, reveal, cash out) |
| `slots.c` | Spark Slots: 3 player-stopped reels, pay table |
| `grid.c` | Spark Grid: 5x5 flip puzzle with row/column hints |
| `bits.c` | Bit Flip: binary target race with a shrinking timer |
| `booth.c` | Photo booth: look and lens pickers, countdown, GIF recording, gallery |
| `include/app_game_layout.h` | Button and eye geometry shared with the view |

```c
app_game_handle_t game = NULL;
const app_game_config_t config = {480, 480, 0}; /* pixels, card seed */
if (app_game_create(&config, &game) == APP_GAME_OK) {
    app_game_add_accel(game, 0.0f, 0.0f, 1.0f);    /* g, one IMU sample */
    app_game_set_pointer(game, 240, 236, true);     /* finger down on the eyes */
    app_game_set_pointer(game, 240, 236, false);    /* release = tap = pet */
    app_game_update(game);                          /* one 30 Hz tick */
    const uint32_t events = app_game_take_events(game);   /* APP_EVENT_* bits */
    app_game_snapshot_t snapshot = app_game_read(game);
    (void)events; (void)snapshot;
    app_game_destroy(&game);
}
```

## Units and rules

- Ticks are 30 Hz updates and wrap modulo 2^32. Coordinates are panel pixels, clamped.
- 20 steps earn 1 spark. The daily goal defaults to 8000 steps and is set in the
  Goal panel (1000..30000 in 1000-step taps); reaching it gives friendship and
  full happiness once per day. Changing the goal never re-awards the same day.
- Walking is the pet's food. `food_need` = step goal / 2; fullness is today's
  steps toward it (0..100) plus snack fullness from the Feed screen, which
  drains 1 point every 2 minutes. Snacks never count toward survival.
- Health: `hp` 0..5 hearts, settled when the day changes (`app_game_set_day`):
  +1 if the day was fed (steps_today reached food_need), -1 if not, and -1 per
  day skipped entirely (device off). At 0 the pet faints: X eyes, snacks and
  games refused (`APP_EVENT_BLOCKED`). Walking food_need steps in a day
  revives it with 1 heart. Without a valid clock no day ends, so health holds.
- Happiness drops 1 point every 3 minutes (twice as fast while hungry);
  petting, food and card wins raise it.
- A swipe is at least 70 px of horizontal travel; a tap moves at most 28 px.
  Left/right swipes move between Home, Steps, Feed, Arcade and, only while
  a camera is detected (`app_game_set_camera`), the Booth page. The Arcade
  shows cabinets (`app_arcade_cabinet_rect`); a tap opens HI-LO, opens Slots
  if owned (else the store at that item), and the SOON cabinet only shakes.
  Inside a game, EXIT (`app_arcade_exit_rect`) returns to the cabinets.
- A downward drag (>= 70 px) starting in the top 90 px opens the menu overlay;
  an upward drag closes any overlay. The menu leads to Store, Style and Goal; their
  BACK button returns to the menu. In the store, left/right swipes change page.
- Wi-Fi: the menu's network line (`app_menu_wifi_rect`) opens the Wi-Fi panel
  unless the state is OFF. With nothing saved (`APP_NET_NO_CONFIG`) or a lost
  network (`APP_NET_FAILED`) it is a SET UP button and the tap also raises
  `APP_EVENT_WIFI_SETUP`. In the panel, the action button
  (`app_wifi_action_rect`) raises `APP_EVENT_WIFI_STOP` while setup runs and
  `APP_EVENT_WIFI_SETUP` otherwise (SET UP / CHANGE WIFI). The model never
  touches Wi-Fi: the runtime acts on these events and reports back through
  `app_game_set_network()` and `app_game_set_setup_ap()` (hotspot name, shown
  while `APP_NET_SETUP`). While that panel shows a running setup the screen
  does not dim or doze, so the hotspot name stays readable.
- Store: colors, pixel styles and animations cost sparks (see `s_store` in
  `app_game.c`). Lime and chunky are free. Buying deducts sparks and equips the
  item; tapping an owned color/style selects it, an owned animation toggles.
  Style only selects owned items; a locked one opens the store at that item.
  Style's SOUND button cycles `sound` ON -> LOUD -> OFF (`APP_EVENT_SETTINGS`,
  saved in the record's former `reserved` byte, so older saves read ON). The
  model only stores it; the runtime's `app_sfx` plays the sounds.
- `owned` uses the `APP_OWN_*_BIT` layout; `app_game_import()` always restores
  the free items and drops selections or animations that are not owned.
- With STEP DANCE on, crossing each 100 total steps raises `APP_EVENT_DANCE`.
- Power: with no touch for 20 s the screen dims (`APP_POWER_DIM`, pet looks
  sleepy); at 30 s it turns off (`APP_POWER_DOZE`), closing panels and going
  home. At or below 15 % battery (not charging) it turns off after 12 s. The
  menu's SLEEP turns it off at once. The touch that wakes the screen is
  swallowed (no tap or swipe), so a pocket touch cannot buy or play.
  Steps keep counting in every power state; steps do not keep the screen on.
- Settings overlay (`APP_OVERLAY_SETTINGS`, 7 rows over 2 swipeable pages):
  SOUND cycles OFF/LOW/MID/HIGH (`app_sound_t` keeps the old saved values:
  0 = MID, 1 = HIGH, 2 = OFF, 3 = LOW), DAILY GOAL and WI-FI open their panels
  (whose BACK returns to Settings), SCREEN SLEEP cycles 15/30/60/120 s (dims for
  the last third), BRIGHTNESS cycles 100/80/60 % (drawn by the view), ABOUT is
  read-only, and holding RESET PET for 3 s starts a new pet but keeps sound,
  goal, sleep and brightness. Sleep/brightness are saved as codes in the former
  `reserved2` bytes (0 = default), so no save migration was needed.
- Battery: `app_game_set_battery()` stores the runtime's reading for display;
  it only changes the doze timeout.
- Spark Slots is a store item (100 sparks); owning it unlocks its arcade
  cabinet, and buying it opens that cabinet. A spin costs 3 sparks; each STOP tap halts the
  leftmost spinning reel after it finishes its symbol plus 0..2 random slip.
  Three of a kind pays 150 (7) / 60 / 30 / 20 / 10 / 8 (shoe); any pair refunds
  the bet. Blind play returns about 72%, so the machine is a spark sink.
- Camera: the runtime reports a camera module in the left slot with
  `app_game_set_camera()`. Only then does the Booth page exist (after Arcade;
  page dots and swipe bounds follow) and does the store list PHOTO BOOTH and
  the filters (`app_game_store_visible*()` map store positions to catalog
  entries). Ownership never changes with the camera. If the camera goes away
  while the Booth page is shown, `booth.unplugged` is set (the view asks to plug
  it in) and after `APP_CAMERA_GONE_S` seconds still without it the page
  returns Home.
- Photo booth is a store item (300 sparks). Before it is bought the Booth page
  is a teaser whose UNLOCK button (`app_booth_unlock_rect`) opens the store at
  it; buying it opens the page. A photo is a lens plus a look. Lenses are
  `APP_ITEM_LENS` items (`lenses_owned`, `APP_OWN_LENS_BIT`): LO RES 32x32 is
  free, MID RES 64x64 costs 80, HI RES 128x128 180. Looks are `APP_ITEM_FILTER`
  items (`filters_owned`, `APP_OWN_FILTER_BIT`): PLAIN is free; DITHER 50,
  SCANLINES 60, GAME-GREEN 80, HALFTONE 100, THERMAL 120, ASCII 120,
  EYE STICKERS 150. Lenses and looks can only be bought once the booth is owned
  (tapping one earlier jumps to the booth's store item). Buying or tapping an
  owned one selects it; both selections are saved. COLOR FILM (200,
  `APP_ITEM_FILM`, `APP_OWN_FILM_BIT` = bit 7 of `lenses_owned`) unlocks the
  film chip (`app_booth_film_rect`), which toggles `booth.color` (B/W or
  COLOR, saved as bit 7 of the record's `lens`, so no save bump). GAME-GREEN
  and THERMAL always shoot B/W; requests carry `color` already resolved.
- Booth flow (`snapshot.booth`): LIVE shows the feed; the side arrows
  (`app_booth_prev_rect`/`next_rect`) cycle owned looks, the lens chip
  (`app_booth_lens_rect`) cycles owned lenses; GIF/SNAP/ROLL are
  `app_booth_button_rect(0..2)`. SNAP: COUNTDOWN 3-2-1 (20 ticks each), FLASH
  (8 ticks) with an `APP_BOOTH_REQ_PHOTO` request, then REVIEW (75 ticks or a
  tap). GIF: the same countdown, then RECORD requests frames 0..7 every 4 ticks
  (`APP_BOOTH_REQ_GIF_FRAME`; the runtime saves on frame 7). The request
  carries the selected lens; the runtime picks the lens the GIF records at
  from what its store keeps (every lens on the ESP-Mosaico, B&W up to MID and
  color LO on the NVS boards). ROLL opens the
  GALLERY: arrows browse (0 = newest), BACK returns, DELETE asks SURE? and then
  sends `APP_BOOTH_REQ_DELETE`; the position text is `PIC 3/40` (index is a
  `uint16_t`, enough for the store's 300-item cap). The model has no limits of
  its own: when the runtime's roll says a photo / GIF at the selected lens and
  film does not fit (`photo_room` / `gif_room`), SNAP / GIF goes straight to
  REVIEW with `APP_BOOTH_RESULT_FULL`; a save that then fails as full reports
  the same.
- The model never holds pixels. The runtime takes one request at a time with
  `app_game_take_booth_request()`, answers with `app_game_booth_result()`, and
  reports `app_game_set_booth(camera, &roll)` every tick (`app_booth_roll_t`:
  counts, fixed slots or 0 for space-limited, free KB and the two room flags).
  The roll is not saved with the pet. Without a
  camera, or while fainted, SNAP/GIF raise `APP_EVENT_BLOCKED` (the gallery
  still works). Leaving the booth (swipe, a panel, screen off, unplug) cancels a
  countdown or recording; a partial GIF is never saved. Capturing keeps the
  screen awake.
- Step sources: `app_game_set_sensor_steps()` (a cumulative hardware count; the
  first reading is a baseline) disables the software detector fed by
  `app_game_add_accel()`. `app_game_add_steps()` credits directly.
- A new `app_game_set_day()` value resets `steps_today`. Values <= 0 mean the
  clock is unknown and are ignored.

## Ownership and threading

The creator owns the handle and serializes every call on one loop; there are no
locks, waits or ISR-safe entry points. Only create/destroy allocate. Snapshots
and `app_game_record_t` are plain values. `app_game_import()` clamps every field,
so a corrupt or older record cannot put the model out of range.

`app_game_record_t` is version 6 (76 bytes): v5 appended the Bit Flip best, v6
the owned and selected photo looks and lenses; v2..v5 records are prefixes and
migrate in the runtime.

`save_seq` increments on every persistent change; the runtime decides when to
write. Events in `APP_EVENTS_SAVE_NOW` (spending, settings, goal) deserve a
prompt save; step drift can be batched to limit flash wear.

Test the rules with a native C compiler or the Host simulator's replay; no
firmware or graphics dependency is needed.
