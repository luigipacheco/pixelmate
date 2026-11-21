# Pixel Pal

A pedometer virtual pet for the **ESP-Mosaico** (ESP32-S31, 480×480 touch screen),
built on Raylib Lite Engine. The pet is a pair of pixel-art robot eyes that
react to how much you walk, what you feed it and how you play with it.

- **Steps → sparks:** the BMI270's on-chip step counter keeps counting while
  the pet is idle; every 20 steps earns 1 spark. Daily goal: 8000 steps by
  default, adjustable from 1000 to 30000 in the Goal panel.
- **Feed:** spend sparks on CELL (10), CHIP (25) or MEGA (60) snacks.
- **Cards:** high-low. Deal for 5 sparks, each right guess doubles the pot,
  cash out any time (auto cash-out after 5 wins).
- **Moods:** idle, happy, sleepy, hungry, sad, walking, eating, thinking,
  surprised and grumpy, each with its own eye shape, springy motion, blinks
  and gaze that follows your finger.
- **Store:** walking pays for unlocks. Buy eye colors (40–120 sparks), finer
  pixel styles (80–150) and animations (100–300): a spark trail as you walk, a
  dance every 100 steps, star eyes when happy and rainbow eyes while walking.
  Home always shows how many steps until your next unlock.
- **Spark Slots (unlockable, 250 sparks):** a three-reel slot machine where
  you stop each reel yourself; 3 sparks a spin, 7-7-7 pays 150.
- **Low-energy mode:** after 20 s without a touch the pet dozes off and the
  screen dims; at 30 s the screen turns off (12 s on low battery). SLEEP in
  the menu does it at once. Steps keep counting; tap the screen to wake.
- **Battery:** percentage in the pull-down menu; a top-bar icon appears while
  charging or when the battery is low.
- **Style:** pick among the pixel sizes and colors you own.
- Progress is saved to NVS.

Swipe left/right between Home, Steps, Feed, Cards (and Slots once bought). Pull down from the top
edge for the menu (Store, Style, Goal); swipe up to close. Tap the eyes to pet.

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
software pedometer.

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
| `main/game_entry.c` | Selects the runtime module for the Engine |
| `game.sim.json` | Sources shared by the Host simulator and firmware |

Dependencies flow `main -> app_game_runtime -> app_game`; the model never
depends on drawing or hardware. Component READMEs document contracts.

## Roadmap

More pet functions, then an AI assistant using the board's mic, speaker and AI button.
