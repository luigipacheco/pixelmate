# app_sfx

8-bit sound effects for PixelMate. Native builds only: the Host simulator does
not link audio, so this component is not in `game.sim.json` and the runtime
calls it inside `#if defined(RAYLIB_LITE_GAME_NATIVE)`.

| File | Responsibility |
| --- | --- |
| `app_sfx.c` | Opens the codec, loads the clips, maps events to cues |
| `../../assets_src/generate_sfx.py` | Synthesizes the clips (pulse, triangle, noise) |
| `../../assets_src/game_assets.json` | Packs every `sfx_*.wav` into a `.sound` asset |

```c
/* In the runtime's update(), after the model tick: */
const uint32_t events = app_game_take_events(game);
const app_game_snapshot_t snapshot = app_game_read(game);
app_sfx_update(events, &snapshot);
/* In shutdown(): */
app_sfx_close();
```

## How it works

- Assets: the native build runs `assets_src/generate_sfx.py` (via
  `raylib_lite_native_embed_assets` in `main/CMakeLists.txt`), which writes 27
  deterministic 24 kHz mono 16-bit WAVs; the engine packer turns them into
  `.sound` assets embedded in flash (about 380 KB). The WAVs are build outputs
  and are not committed. Every clip is under 1 s so the packer keeps 16-bit PCM
  instead of 4-bit ADPCM. Loudness per clip is the `PEAK` table in the script.
- Playback: the engine's game audio (`raylib_lite_game_audio.h`, ES8311 DAC at
  24 kHz). The first `app_sfx_update()` opens the codec, loads every clip and
  plays the boot chime. 27 clips exceed the engine default of 20, so
  `sdkconfig.application.defaults` sets `CONFIG_ESP_MOSAICO_AUDIO_CLIPS=32`.
- Volume: the Style panel's SOUND button cycles `app_sound_t` ON (default) ->
  LOUD -> OFF; it is saved in the pet record. ON and LOUD set the mixer master
  gain to 0.6 / 1.0 (`LEVEL_GAIN`); OFF plays nothing.
- Screen off (`APP_POWER_DOZE`): nothing plays, and a pending sting is
  dropped. Waking from it plays the boot chime. Dimmed screens still play.
- Battery: while audio is open the engine streams to the codec nonstop
  (silence included), keeping I2S, the DAC and the speaker amp powered. So the
  codec is closed while the screen is off or the sound is OFF, and reopened
  on wake; the clips are reloaded then, and the first cue (usually the wake
  chime) waits up to 1 s for them.
- One cue per tick, highest priority first; a card flip or the last slot reel
  can queue one delayed sting (win / lose / jackpot).

| Event (`APP_EVENT_*` or snapshot change) | Sound |
| --- | --- |
| `REVIVED` | `sfx_revive` rising sweep + arpeggio |
| `fainted` became true | `sfx_faint` sad descending |
| `GOAL` | `sfx_goal` fanfare |
| `FED` | `sfx_fed` victory arpeggio |
| `SLOT_DONE` | `sfx_reel`, then `sfx_jackpot` (>= 30 sparks) / `sfx_win` / `sfx_lose` |
| `GRID_DONE` | `sfx_clear` (cleared) / `sfx_coins` (cashed) / `sfx_zap` (dud) |
| `GRID_FLIP` | `sfx_tile`, or `sfx_tile_bonus` for x2 / x3 |
| `CARD_REVEAL` | `sfx_flip`, then `sfx_win` / `sfx_lose` (tie: none) |
| `CASH_OUT` | `sfx_coins` |
| `BOUGHT` | `sfx_buy` cash register |
| `ATE` | `sfx_eat` crunch |
| `SLOT_STOP` / `SLOT_SPIN` | `sfx_reel` clack / `sfx_spin` lever |
| `CARD_DEAL`, `CARD_NEXT` | `sfx_deal` |
| `PETTED` | `sfx_pet` chirp |
| `NO_SPARKS`, `BLOCKED` | `sfx_buzz` low "eh-eh" |
| power DOZE -> awake | `sfx_wake` boot chime |
| `OVERLAY` | `sfx_open` (menu opened) / `sfx_close` (closed) / `sfx_tap` (panel to panel) |
| `SETTINGS`, `ARCADE`, `WOKE` | `sfx_tap` click |
| `SCREEN`, `STORE_PAGE` | `sfx_swipe` |
| `SPARK` | `sfx_spark` coin |
| `STEP` crossing a multiple of 10 total steps | `sfx_step` soft tick |

To add a sound: add a function and a `PEAK` entry in `generate_sfx.py`, add the
`SFX_*` value and its `.sound` name in `app_sfx.c`, map it in `pick()`, and keep
the clip count within `CONFIG_ESP_MOSAICO_AUDIO_CLIPS`.

## Not verified on Host

The Host build has no audio. Device listening (level, balance, timing) is the
acceptance test, per the engine's `docs/audio-design.EN.md`.
