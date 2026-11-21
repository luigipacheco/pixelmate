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
- Hunger (fullness) drops 1 point every 2 minutes, happiness every 3 minutes
  (twice as fast while hungry). Petting, food and card wins raise them.
- A swipe is at least 70 px of horizontal travel; a tap moves at most 28 px.
  Left/right swipes move between Home, Steps, Feed and Cards.
- A downward drag (>= 70 px) starting in the top 90 px opens the menu overlay;
  an upward drag closes any overlay. The menu leads to Store, Style and Goal; their
  BACK button returns to the menu. In the store, left/right swipes change page.
- Store: colors, pixel styles and animations cost sparks (see `s_store` in
  `app_game.c`). Lime and chunky are free. Buying deducts sparks and equips the
  item; tapping an owned color/style selects it, an owned animation toggles.
  Style only selects owned items; a locked one opens the store at that item.
- `owned` uses the `APP_OWN_*_BIT` layout; `app_game_import()` always restores
  the free items and drops selections or animations that are not owned.
- With STEP DANCE on, crossing each 100 total steps raises `APP_EVENT_DANCE`.
- Power: with no touch for 20 s the screen dims (`APP_POWER_DIM`, pet looks
  sleepy); at 30 s it turns off (`APP_POWER_DOZE`), closing panels and going
  home. At or below 15 % battery (not charging) it turns off after 12 s. The
  menu's SLEEP turns it off at once. The touch that wakes the screen is
  swallowed (no tap or swipe), so a pocket touch cannot buy or play.
  Steps keep counting in every power state; steps do not keep the screen on.
- Battery: `app_game_set_battery()` stores the runtime's reading for display;
  it only changes the doze timeout.
- Spark Slots is a store item (250 sparks); owning it adds `APP_SCREEN_SLOTS`
  as the last swipe page. A spin costs 3 sparks; each STOP tap halts the
  leftmost spinning reel after it finishes its symbol plus 0..2 random slip.
  Three of a kind pays 150 (7) / 60 / 30 / 20 / 10 / 8 (shoe); any pair refunds
  the bet. Blind play returns about 72%, so the machine is a spark sink.
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

`save_seq` increments on every persistent change; the runtime decides when to
write. Events in `APP_EVENTS_SAVE_NOW` (spending, settings, goal) deserve a
prompt save; step drift can be batched to limit flash wear.

Test the rules with a native C compiler or the Host simulator's replay; no
firmware or graphics dependency is needed.
