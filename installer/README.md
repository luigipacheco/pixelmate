# Web installer

A static page that installs PixelMate from the browser over USB (Web Serial, Chrome or Edge on
a computer): choose the board, choose the firmware, connect and install. The flow follows
[CrossPoint Reader's installer](https://github.com/crosspoint-reader/crosspoint-tools);
flashing is [esptool-js](https://github.com/espressif/esptool-js) 0.7.1, vendored in `vendor/`.

| Board | Chip | Writes | Save area (Fresh start clears it) |
| --- | --- | --- | --- |
| ESP-Mosaico | ESP32-S31 | partition table `0x8000`, app `0x210000` (like the Mosaico system update; bootloader, launcher and system data untouched) | `nvs` `0x200000` |
| M5Stack StackChan | ESP32-S3 | bootloader `0x0`, partition table `0x8000`, app `0x20000` | `nvs` `0x9000` |
| M5Stack StopWatch | ESP32-S3 | bootloader `0x0`, partition table `0x8000`, app `0x20000` | `nvs` `0x9000` |

The page checks that the connected chip matches the board, and checks every image against
the sizes and SHA-256 sums in `firmware.json` before it writes. It never erases the whole chip.
**Back up current firmware** reads the whole flash into a `.bin` (restore with
`esptool --port <port> write-flash 0 <file>`).

## Files

- `boards.json`: the boards, what is written where, the save area and the connect tips.
- `firmware/<board>/`: staged firmware (`firmware.json` + images). Not in git; see below.
- `index.html`, `installer.css`, `installer.js`: the page. No build step.

## Stage firmware

Build a board with ESP-IDF, then from the repo root:

```sh
python tools/stage_installer_firmware.py mosaico   build                    # built in the esp-mosaico-vibe workspace
python tools/stage_installer_firmware.py stackchan boards/stackchan/build
python tools/stage_installer_firmware.py stopwatch boards/stopwatch/build
```

It copies only the images `boards.json` lists, refuses a build for the wrong chip, and writes
`firmware.json` (version from `git describe`, or `--version v1.1.0`). A board without staged
firmware shows "No firmware staged" and can still take a custom app image under Advanced.

## Try it locally

```sh
python3 -m http.server 8000 -d installer
```

Open <http://localhost:8000> in Chrome or Edge (Web Serial needs `localhost` or HTTPS).

## Host it

Any static host over HTTPS works: publish this folder with the staged `firmware/` in it.
On GitHub Pages, `.github/workflows/installer-pages.yml` does this on every push to `main`:
it builds the StopWatch in the `espressif/idf:release-v6.2` image, stages it and publishes
`installer/`. Turn it on once with Settings > Pages > Source: GitHub Actions.
