# Boards

PixelMate is one game that runs on several devices. Everything a device
needs that the others don't lives in its folder here; the game itself
(`../main`, `../components`) is shared.

| Board | Chip | Build from | Pet |
| --- | --- | --- | --- |
| `mosaico` | ESP32-S31 | the repo root (links ESP-Iris, Vibe Mode, Recovery) | walking pet |
| `stackchan` | ESP32-S3 (M5Stack CoreS3) | `boards/stackchan` | desk pet with a neck |
| `stopwatch` | ESP32-S3R8 (M5Stack StopWatch) | `boards/stopwatch` | walking pet on a round screen |

## How a board says what it has

Each board has a `board.cmake` that lists one driver for every kind of
hardware, `none` when it has no such part, plus game features:

```cmake
pixelmate_name("STACK-CHAN")      # shown on the About page
pixelmate_part(POWER ina226)      # battery monitor chip
pixelmate_part(NECK scs0009)      # pan/tilt servos
pixelmate_part(PEDOMETER none)    # nobody walks with it
pixelmate_feature(DESK_PET)       # pats earn sparks, no step screens
```

The kinds and their drivers are listed in `../cmake/pixelmate_board.cmake`.
Each hardware component (`../components/app_*_hw`) picks its source file from
the board's choice: drivers are named after the part (`app_power_hw_ina226.c`),
so two boards with the same chip share the driver. Nothing picks code by chip
type (`IDF_TARGET`), because different boards share a chip.

The game runtime gets `APP_DESK_PET` / `APP_POCKET_MODE` / `APP_ROUND_SCREEN`
for the features and
`PIXELMATE_HAS_<KIND>` for every part the board has, so the game asks "is
there a neck?", never "is this a StackChan?".

## Adding a board

1. `boards/<name>/board.cmake`: the parts list (every kind, `none` for missing
   parts; the build stops and names any kind left out).
2. `boards/<name>/CMakeLists.txt`: copy `stackchan/CMakeLists.txt`, change the
   board name, project name and `RAYLIB_LITE_BOARD`.
3. `boards/<name>/components/<name>_board`: the Raylib Lite board adapter
   (display, touch, audio), plus `sdkconfig.defaults`, `partitions.csv` and an
   ESP Board Manager profile if it uses one.
4. A driver for any part no other board has yet: a new `app_<kind>_hw_<part>.c`
   in the matching component and one more branch in its `CMakeLists.txt`.
5. Build every board after changing shared code.
