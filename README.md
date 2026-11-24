# PixelMate

A pedometer virtual pet for the **ESP-Mosaico** (ESP32-S31, 480×480 touch screen),
built on Raylib Lite Engine. The pet is a pair of pixel-art robot eyes that
react to how much you walk, what you feed it and how you play with it.

(Formerly "Pixel Pal". The project folder, CMake target and save storage keep
the `pixel_pal` name so existing pets carry over.)

The same game also builds for two M5Stack devices (one codebase, one firmware
per board, see [boards/README.md](boards/README.md)):

- **M5Stack StackChan** (CoreS3): Bitsy is a desk pet with a neck that earns
  sparks from pats instead of steps
  ([boards/stackchan/README.md](boards/stackchan/README.md)).
- **M5Stack StopWatch** (round AMOLED watch): a walking pet like the Mosaico,
  with a round layout and pocket mode on its yellow button
  ([boards/stopwatch/README.md](boards/stopwatch/README.md)).

- **Steps → sparks:** the BMI270's on-chip step counter keeps counting while
  the pet is idle; every 20 steps earns 1 spark. Daily goal: 8000 steps by
  default, adjustable from 1000 to 30000 in the Goal panel.
- **Walking is food:** the pet needs half your daily goal in steps each day.
  Fed days add a health heart (max 5), missed days take one; at 0 the pet
  faints and only walking that day's food brings it back.
- **Snacks:** spend sparks on CELL (10), CHIP (25) or MEGA (60) for a temporary
  fullness boost (they don't count toward survival).
- **Arcade:** a row of pixel cabinets with attract-mode demos; tap one to
  play (EXIT returns). Every game is bought in the store, in unlock order:
  Spark Slots (100), Spark Stack (150), Spark Grid (200), Bit Flip (250),
  Bitsy Says (300) and HI-LO (400), the best earner, last.
- **HI-LO (unlockable cabinet, 400 sparks):** deal for 5 sparks, each right
  guess doubles the pot, cash out any time (auto cash-out after 5 wins).
- **Moods:** idle, happy, sleepy, hungry, sad, walking, eating, thinking,
  surprised and grumpy, each with its own eye shape, springy motion, blinks
  and gaze that follows your finger.
- **Store:** walking pays for unlocks. Buy eye colors (40–120 sparks), finer
  pixel styles (80–150) and animations (100–300): a spark trail as you walk, a
  dance every 100 steps, star eyes when happy and rainbow eyes while walking.
  The menu shows how many steps until your next unlock; Home stays just the pet.
- **Spark Slots (unlockable cabinet, 100 sparks):** a three-reel slot machine
  where you stop each reel yourself; 3 sparks a spin, 7-7-7 pays 150. Pure
  luck, and it keeps a house edge.
- **Spark Stack (unlockable cabinet, 150 sparks):** a bar slides back and
  forth; tap to drop it on the tower and the overhang is cut off. 5 sparks a
  run; all or nothing: only reaching the top (12 rows) pays, 20 sparks +2 per
  perfect drop (up to 40).
- **Bitsy Says (unlockable cabinet, 300 sparks):** Bitsy's eyes flash a growing
  sequence of four colors (the pads light up with tones); repeat it. 5 sparks a
  run, 1 spark per round completed (up to 30).
- **Bit Flip (unlockable cabinet, 250 sparks):** flip 8 switches (128..1) to
  match the target number before the timer runs out; every hit brings a new
  number and a shorter timer. 5 sparks a run, 1 spark per hit (up to 30).
- **Spark Grid (unlockable cabinet, 200 sparks):** a 5x5 logic flip puzzle.
  Tiles hide x1/x2/x3 or a dud; row/column hints show totals and dud counts.
  10 sparks a round, each flip multiplies the score, CASH banks it. Levels
  1-5 like the original: clear a board to climb (level 5 boards pay up to
  576), a dud or CASH drops you to the number of tiles you flipped. Long-press
  a hidden tile to cycle a memo (X guards it from taps, or a note 1/2/3).
- **Low-energy mode:** after 20 s without a touch the pet dozes off and the
  screen dims; at 30 s the screen turns off (12 s on low battery). SLEEP in
  the menu or a press of the AI button does it at once. Steps keep counting;
  tap the screen or press the AI button to wake.
- **TAP STEPS (Settings, off by default):** an accessibility mode for people who
  can't walk (or boards without a step counter): every 10 pats on Bitsy count
  as one step, with no daily cap.
- **TOOLS (a page after the arcade):** Flipper-style sensor apps; each use
  earns Bitsy tools XP and levels. SENSORS (live graphs: accelerometer,
  gyroscope, magnetometer, battery, steps), STEP CAL (walk 50 counted steps to
  correct the step counter; saved), COMPASS / METAL detector (the board's
  BMM150 magnetometers; beeps faster near metal), LEVEL (bubble, tap to zero),
  SEISMO (vibration trace) and WI-FI scanner. With the Interaction module in
  the RIGHT slot: IR REMOTE (NEC codes: power, volume, channel, a TV OFF blast),
  GUARD (PIR motion alarm with a visitor count) and LIGHT (light meter and a
  lamp). The module's LEDs also glow with Bitsy's mood (red pulse when hungry,
  rainbow at the goal), walking past wakes Bitsy, and its two keys act as arcade
  buttons (HI/LO, slots, Spark Stack drop, a pat on the home screen).
- **Easter eggs (spoilers):** 1000 lifetime pats unlock LOVE EYES (hearts when
  happy); ten quick taps on the spark counter make it rain sparks (just for
  show); tapping the version on About 7 times rolls the credits; three hard
  shakes make Bitsy dizzy (spiral eyes); holding it upside down makes it panic;
  coming back after 3+ days gets an "I MISSED YOU"; 10,000 steps in a day earn a
  MARATHON headband; the clock throws New Year fireworks at midnight on Jan 1.
- **DEV MODE (secret):** on Settings > ABOUT enter the Konami code: swipe up,
  up, down, down, left, right, left, right, then tap the left half (B) and the
  right half (A). Each input lights a glyph; unlocking plays a jackpot. A
  sandbox with infinite sparks: its Settings row (page 3) turns it off, and
  turning it off or restarting restores the real sparks and items.
- **Idle, with a clock:** when the screen times out or you press the AI
  button once, Bitsy idles. Everything keeps running, and when the time is
  known the screen shows a clock instead of going black: big pixel digits with
  Bitsy's eyes as the colon, the date and today's steps, drifting a few pixels
  each minute to protect the AMOLED. On USB it is a bedside glow and the eyes
  blink, hop on the hour and doze at night. On battery it is dimmer, changes
  once a minute, and Wi-Fi and sound switch off. A tap or the AI button wakes
  it.
- **Pocket mode (deep sleep, on battery):** hold the AI button 1.5 s, or leave
  Bitsy idle for 10 minutes. It saves and sleeps with only the step counter
  running. The AI button cannot wake the chip on this board, so a tap restarts
  it and shows the clock with PRESS AI BUTTON: press it within 8 seconds to
  wake Bitsy, or it goes back to sleep (a bump in a pocket costs only that).
  Steps walked meanwhile are added. There is no deep sleep on USB: there a
  long press only turns the screen fully off.
- **Battery:** percentage in the pull-down menu; a top-bar icon appears while
  charging or when the battery is low.
- **Wi-Fi setup:** first time, pull down the menu and tap **SET UP WIFI**. The
  board shows a hotspot name (`PIXELMATE-XXXX`); join it from a phone, pick your
  Wi-Fi on the page that opens (or go to `192.168.4.1`) and save. The Wi-Fi set
  in Vibe Mode is used when nothing was saved here.
- **Wi-Fi stats:** joins the saved Wi-Fi, syncs the clock, and
  serves a stats page at `http://pixelmate.local` (or the IP in the menu):
  today, food, health, 7-day chart, streak, sparks and battery. Opening it once
  sets the pet's timezone.
- **Style:** pick among the pixel sizes and colors you own.
- **Settings** (pull-down menu): sound volume (off/low/mid/high), daily goal,
  Wi-Fi, screen sleep (15 s to 2 min), brightness (60-100 %), about, and
  RESET PET (hold 3 s; keeps your settings).
- **Sound:** short 8-bit blips for sparks, petting, eating, buying, menus and
  every arcade game; a victory jingle when the pet is fed. Silent while the
  screen is off.
- **Photo booth (needs a camera module, 300 sparks):** plug an OV3640 or
  SC101IOT camera board into the **left** slot (any time; no reboot) and a
  Booth page appears after the Arcade. Tap SNAP for a 3-2-1 photo or GIF for
  an 8-frame clip; ROLL browses and deletes them. A photo is a lens plus a
  look: LO RES 32x32 is free, MID RES 64x64 (80) and HI RES 128x128 (180) are
  sold; COLOR FILM (200) adds real 16-color photos (B/W chip next to the lens);
  WEBCAM (250) adds an ON AIR chip that streams the camera over Wi-Fi to
  `http://<board-ip>:81/cam?k=<code>` (MJPEG, code shown on screen, new each time);
  PLAIN is free, DITHER, SCANLINES, GAME-GREEN, HALFTONE, THERMAL, ASCII
  and EYE STICKERS are sold (50-150). Photos keep the pet's palette. Up to 12
  photos and 2 GIFs are kept in NVS (HI RES photos fill it sooner), and the
  stats page links to a gallery (`/photos`) to save them as PNG/GIF. Without a
  camera the page and its store items are hidden; what you bought stays owned.
- Progress is saved to NVS.

Swipe left/right between Home, Steps, Feed, Arcade and (with a camera) Booth. Pull down from the top
edge for the menu (Store, Style, Settings); swipe up to close. Tap the eyes to pet.

## Build and run

This project lives inside an [esp-mosaico-vibe](https://github.com/esp-mosaico)
workspace at `projects/pixel_pal` and uses the workspace's pinned ESP-IDF,
BSP, utils and Raylib Lite Engine submodules. From the workspace root:

```sh
python mosaico.py game sim --project projects/pixel_pal          # browser simulator
python mosaico.py game build --project projects/pixel_pal
python mosaico.py iris system-update --project projects/pixel_pal
```

The Host simulator needs a C compiler (`cc`, `gcc` or `clang` on PATH) and Pillow.
In the simulator the space key adds 50 steps; replay `imu` events drive the
software pedometer; the right key unplugs/replugs the pretend camera (the booth
shows a synthetic scene).

A fresh checkout must generate the Board Manager configuration once before the
first firmware build (see the workspace's `docs/game-development.md`):

```sh
IDF_EXTRA_ACTIONS_PATH=projects/pixel_pal/managed_components/espressif__esp_board_manager \
  idf.py -C projects/pixel_pal bmgr \
  -c submodule/raylib-lite-engine/examples/boards/esp-mosaico/bmgr -b esp_mosaico
```

## Layout

| Path | Responsibility |
| --- | --- |
| `components/app_game` | Pure C pet rules: steps, stats, moods, gestures, cards |
| `components/app_game_runtime` | Engine adapter, pixel renderer, eyes, screens, save |
| `components/app_pedometer_hw` | BMI270 on-chip step counter (device only) |
| `components/app_power_hw` | Screen power and battery gauge (device only) |
| `components/app_net` | Wi-Fi, network time, mDNS, stats web server (device only) |
| `components/app_sfx` | 8-bit sound effects from model events (device only) |
| `components/app_photo` | Photo lenses, looks, palettes, PNG/GIF encoders (pure C) |
| `components/app_photo_store` | Photo/GIF gallery in NVS (RAM in the simulator) |
| `components/app_camera_hw` | Hot-pluggable left-slot camera, 128x128 feed (device only) |
| `assets_src/generate_sfx.py` | Generates the sound clips at build time |
| `main/game_entry.c` | Selects the runtime module for the Engine |
| `game.sim.json` | Sources shared by the Host simulator and firmware |

Dependencies flow `main -> app_game_runtime -> app_game`; the model never
depends on drawing or hardware. Component READMEs document contracts.

## Roadmap

More pet functions, then an AI assistant using the board's mic, speaker and AI button.
