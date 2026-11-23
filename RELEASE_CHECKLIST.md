# PixelMate v1.0.0: Mosaico Ideas release checklist

Prepared 2026-11-22, refreshed the same night for deep-sleep pocket mode. `release/v1` is merged into main and the upload ZIP is
built and validated from main. Nothing has been uploaded, nothing was installed
on the board, and no `account login` was run.

All commands run from the workspace root
(`C:\Users\luigi\Documents\GitHub\esp-mosaico-vibe`) in PowerShell.

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
- [x] **Firmware bundle** from main 3cab7e0 + docs (includes the
      Interaction module detection fix, MODULE LIGHTS, the battery gauge
      profile fix and deep-sleep pocket mode with tap-to-wake):
      `build/pixel_pal-system-update.irisfw`, 1,337,987 bytes, SHA-256
      `b8c98e8fb01954695189cf8cb6dd3610d443e129f7e886d6ac820ccda2e32189`,
      `project_version` **1.0.0**. No compiler warnings (CMake
      component-validation warnings only). The board runs the same code from
      the feat/deep-sleep branch plus the sleep-test endpoint, now removed.
- [x] **Upload ZIP** `build/pixelmate-v1.0.0-ideas.zip`, 1,564,113 bytes,
      22 files, SHA-256
      `f851da6dd6861f457265212c34c4bcf005adb361fe9d075199b90c476649f73d`.
      `project validate` on the host says `valid`, version 1.0.0.

## Before uploading

1. **Confirm the "tested on hardware" paragraph** in `ideas/README.md`
   ("Good to know"). It says these were checked on the board: step counting
   with the screen off, saving, battery, dimming/sleep, Wi-Fi + stats page +
   daily health, sounds, photo booth with hot-plug. It says TOOLS and the
   Interaction module had the least time on hardware, the setup hotspot was
   not tried from a phone and the OV3640 is untested. Correct it if needed
   and re-package (below).

2. **GitHub repo name.** Done 2026-11-22: the repo is now
   `https://github.com/luigipacheco/pixelmate` (old URL redirects); the manifest
   links there and the ZIP was re-packaged.

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
python mosaico.py project upload projects/pixel_pal/build/pixelmate-v1.0.0-ideas.zip --create
```

The server defaults to `https://mosaico.ai` (or `MOSAICO_IDEAS_SERVER`). Check
the reply: `status: "draft"`, `version: "1.0.0"`, the `application_id` and the
`draft_url`. Submitting for review or publishing is a separate step on the
site. For later versions use `--update <application_id>` instead of `--create`.

## Re-packaging (after any change to the README, manifest or firmware)

1. If the firmware changed, rebuild the bundle (IDF env exported):

   ```powershell
   idf.py -C projects/pixel_pal -B projects/pixel_pal/build -D ESP_IRIS_PYTHON=C:\Users\luigi\AppData\Local\esp-mosaico\runtimes\esp-iris\b09887e895e25207\py3.11\Scripts\python.exe system-update-bundle
   ```

2. Package in a throwaway Linux container. Windows has no libraqm, and Debian
   bookworm's libraqm is too old (missing `raqm_get_par_resolved_direction`),
   so use trixie. Packaging also validates the firmware, which needs the
   ESP-Iris host requirements:

   ```powershell
   docker run --rm -v "${PWD}:/ws" -w /ws python:3.11-slim-trixie sh -c "apt-get update -qq && apt-get install -y -qq libraqm0 && pip install -q -r submodule/esp-mosaico-utils/mosaico-tools/requirements-ideas.txt -r submodule/esp-mosaico-utils/ESP-Iris/components/esp_iris/tools/requirements.txt && python mosaico.py project package --project projects/pixel_pal --output projects/pixel_pal/build/pixelmate-v1.0.0-ideas.zip"
   ```

3. Validate on the host (the host Python now has the Ideas requirements):

   ```powershell
   python mosaico.py project validate projects/pixel_pal/build/pixelmate-v1.0.0-ideas.zip
   ```

## Rebuild notes

- Screenshots: `python projects/pixel_pal/tools/screenshots.py projects/pixel_pal/ideas/screens`
  (MSYS2 ucrt64 gcc on PATH; about 5 minutes; it rewrites
  `assets/generated/*` line endings, so run `git checkout -- assets/generated`
  afterwards). Then `python projects/pixel_pal/tools/make_cover.py`.
- Board Manager gotcha: running `idf.py bmgr -c .../esp-mosaico/bmgr -b esp_mosaico`
  in a fresh checkout picks the `esp_mosaico` board from the managed
  `espressif__esp_boards` package instead of the engine's profile, and the
  build then fails in the engine's `mosaico_setup_imu.c`
  (`dev_custom_imu_sensor_config_t` undeclared). `projects/pixel_pal` has the
  right generated `components/gen_bmgr_codes` (git-ignored) and
  `dependencies.lock`, so build there. Worth reporting upstream.
