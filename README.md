# PixelMate

A pedometer virtual pet for the **ESP-Mosaico** (ESP32-S31, 480×480 touch screen),
built on Raylib Lite Engine. The pet is a pair of pixel-art robot eyes that
react to how much you walk, what you feed it and how you play with it.

(Formerly "Pixel Pal". The project folder, CMake target and save storage keep
the `pixel_pal` name so existing pets carry over.)

There is also a build for the **M5Stack StackChan** (CoreS3), where Bitsy is a
desk pet that earns sparks from pats instead of steps: see
[stackchan/README.md](stackchan/README.md).

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
  Spark Slots (100), Spark Grid (200), Bit Flip (250) and HI-LO (400), the
  best earner, last.
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
- **Bit Flip (unlockable cabinet, 250 sparks):** flip 8 switches (128..1) to
  match the target number before the timer runs out; every hit brings a new
  number and a shorter timer. 5 sparks a run, 1 spark per hit (up to 30).
- **Spark Grid (unlockable cabinet, 200 sparks):** a 5x5 logic flip puzzle.
  Tiles hide x1/x2/x3 or a dud; row/column hints show totals and dud counts.
  10 sparks a round, each flip multiplies the score, CASH banks it.
- **Low-energy mode:** after 20 s without a touch the pet dozes off and the
  screen dims; at 30 s the screen turns off (12 s on low battery). SLEEP in
  the menu does it at once. Steps keep counting; tap the screen to wake.
- **Pocket mode (on battery):** 3 s after the screen goes dark, Wi-Fi stops
  and the chip light-sleeps; the BMI270 keeps counting steps. Wake it with the
  AI button (touch does not wake it in pocket mode). It wakes itself every 10
  minutes to log steps and settle the day. On USB power it stays awake so
  installs and logs keep working.
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
