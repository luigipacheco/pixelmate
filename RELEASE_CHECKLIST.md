# PixelMate v1.0.0: Mosaico Ideas release checklist

Prepared 2026-11-22 on branch `release/v1`. Nothing has been uploaded, nothing
was installed on the board, and no `account login` was run.

All commands run from the workspace root
(`C:\Users\luigi\Documents\GitHub\esp-mosaico-vibe`) in PowerShell. Paths below
use `projects/pixel_pal_release`; once `release/v1` is merged into main, use
`projects/pixel_pal` instead (the firmware must then be rebuilt there).

## Ready

- [x] **Screenshots** in `ideas/screens/` (480×480, host simulator), from
      `tools/screenshots.py`: 01 home, 02 steps ring, 03 arcade (row 1),
      04 Spark Grid with the LV badge, 05 Bitsy Says (red pad, red eyes),
      06 TOOLS, 07 photo booth in COLOR, 08 clock mode, 09 About.
- [x] **Cover** `ideas/cover.png` (1920×1080) from `tools/make_cover.py`:
      DejaVu fonts, "by Luis Pacheco, www.luigipacheco.com", home, arcade,
      booth and TOOLS screens.
- [x] **Ideas page** `ideas/README.md`: current features, arcade unlock order
      and payouts, Spark Grid levels, photo looks, webcam, TOOLS and the
      Interaction module, clock/pocket/TAP STEPS, what was tested where,
      pan & tilt as "coming next". No easter eggs, no DEV MODE.
- [x] **Changelog** `ideas/CHANGELOG.md` (first release).
- [x] **Manifest** `mosaico-ideas.json` (schema `mosaico-ideas/project-release/1`):
      9 covers (cover + screens 01-08), About as a resource (it is shown in
      the README), license Apache-2.0 (LICENSE and NOTICE are in the repo),
      zh-CN and en device presentations.
- [x] **Firmware bundle** built in this worktree (`system-update-bundle`):
      `build/pixel_pal-system-update.irisfw`, 1,313,882 bytes, SHA-256
      `8f38089c1513e2132c0873c105c59dfd71b3a570bddd5a26f143f71ac33d203d`.
      `build/project_description.json` says `project_version` **1.0.0**
      (from `CONFIG_APP_PROJECT_VER` in sdkconfig.defaults). No compiler
      warnings (3 CMake component-validation warnings only). Not installed on
      the board. The manifest's `firmware` path points at this file, so the
      bundle must exist (rebuild it) wherever you run `project package`.

## Still blocking

1. **Packaging needs Python packages and libraqm.** Tried
   `python mosaico.py project package --project projects/pixel_pal_release --output projects/pixel_pal_release/build/pixelmate-v1.0.0-ideas.zip`;
   it stops at `ModuleNotFoundError: No module named 'jsonschema'` (the first
   import; freetype-py and libraqm would fail next, when it generates the
   device name/description). No ZIP exists yet.

   No Python on this machine has everything (checked: system Python 3.11 and
   3.13, the ESP-IDF env, both esp-iris runtimes, WSL Ubuntu):

   | Needed | System Python 3.11 (runs `mosaico.py`) |
   |---|---|
   | jsonschema >= 4.23 | missing |
   | markdown-it-py >= 3 | missing |
   | freetype-py >= 2.4 | missing |
   | Pillow >= 10.4, < 12 | 10.2.0 (too old) |
   | fonttools | 4.53.0 (ok) |
   | libraqm (system library) | not on the machine |

   `project upload` also validates the ZIP, so the host Python needs
   jsonschema, markdown-it-py and Pillow even if the ZIP is packaged elsewhere.
   Only packaging (generating the device name/description images) needs
   freetype-py and libraqm.

   **Fix (needs your OK; it installs software):**

   a) Host Python, needed for upload and validation:

   ```powershell
   python -m pip install -r submodule\esp-mosaico-utils\mosaico-tools\requirements-ideas.txt
   ```

   b) Package in a Linux container (Docker Desktop is running here; the tools
   expect the Debian `libraqm0` package). Not tried yet:

   ```powershell
   docker run --rm -v "${PWD}:/ws" -w /ws python:3.11-slim-bookworm sh -c "apt-get update -qq && apt-get install -y -qq libraqm0 && pip install -q -r submodule/esp-mosaico-utils/mosaico-tools/requirements-ideas.txt && python mosaico.py project package --project projects/pixel_pal_release --output projects/pixel_pal_release/build/pixelmate-v1.0.0-ideas.zip"
   ```

   Alternative on Windows only (untested): install libraqm with MSYS2
   (`C:\msys64\usr\bin\pacman.exe -S mingw-w64-ucrt-x86_64-libraqm`), copy
   `C:\msys64\ucrt64\bin\libraqm-0.dll` to `C:\msys64\ucrt64\bin\raqm.dll`
   (Python's `find_library("raqm")` on Windows only looks for `raqm.dll` on
   PATH), put `C:\msys64\ucrt64\bin` on PATH and run the package command
   below. Risk: freetype-py brings its own FreeType DLL while raqm uses
   MSYS2's, so shaping may misbehave.

   Then package and validate on the host:

   ```powershell
   python mosaico.py project package --project projects/pixel_pal_release --output projects/pixel_pal_release/build/pixelmate-v1.0.0-ideas.zip
   ```

   ```powershell
   python mosaico.py project validate projects/pixel_pal_release/build/pixelmate-v1.0.0-ideas.zip
   ```

2. **Confirm the "tested on hardware" paragraph** in `ideas/README.md`
   ("Good to know"). It says these were checked on the board: step counting
   with the screen off, saving, battery, dimming/sleep, Wi-Fi + stats page +
   daily health, sounds, photo booth with hot-plug. It says TOOLS and the
   Interaction module had the least time on hardware, the setup hotspot was
   not tried from a phone and the OV3640 is untested. Please correct it if you
   have since tried TOOLS, the Interaction module, clock mode, the webcam or
   pocket mode on the board (or if any of them was never run on it).

3. **GitHub repo name (open question).** The manifest links to
   `https://github.com/luigipacheco/pixel_pal`. You planned to rename the
   repo to `pixelmate`. If you rename it before publishing, change that URL in
   `mosaico-ideas.json` and re-package (GitHub redirects the old URL, but the
   page should show the new one). If you keep `pixel_pal` for now, nothing to do.

## Upload (creates a private draft; needs your login and your OK)

```powershell
python mosaico.py account status
```

```powershell
python mosaico.py account login --open-browser
```

You approve the device code in the browser yourself. Then create the app as a
new draft from the packaged ZIP (the upload never rebuilds it):

```powershell
python mosaico.py project upload projects/pixel_pal_release/build/pixelmate-v1.0.0-ideas.zip --create
```

The server defaults to `https://mosaico.ai` (or `MOSAICO_IDEAS_SERVER`). Check
the reply: `status: "draft"`, `version: "1.0.0"`, the `application_id` and the
`draft_url`. Submitting for review or publishing is a separate step on the
site. For later versions use `--update <application_id>` instead of `--create`.

## Rebuild notes

- Screenshots: `python projects/pixel_pal_release/tools/screenshots.py projects/pixel_pal_release/ideas/screens`
  (MSYS2 ucrt64 gcc on PATH; about 5 minutes; it rewrites
  `assets/generated/*` line endings, so run `git checkout -- assets/generated`
  afterwards). Then `python projects/pixel_pal_release/tools/make_cover.py`.
- Board Manager gotcha: running `idf.py bmgr -c .../esp-mosaico/bmgr -b esp_mosaico`
  in a fresh checkout today picks the `esp_mosaico` board from the managed
  `espressif__esp_boards` package instead of the engine's profile, and the
  build then fails in the engine's `mosaico_setup_imu.c`
  (`dev_custom_imu_sensor_config_t` undeclared). The release build used a copy
  of main's generated `components/gen_bmgr_codes` (git-ignored, made
  2026-11-20 from the engine profile) and main's `dependencies.lock`.
  `projects/pixel_pal` already has both, so building there after the merge
  works as before. Worth reporting upstream (engine docs/bmgr), not fixed here.
